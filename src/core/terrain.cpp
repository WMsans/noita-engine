#include "terrain.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>

#include "coords.h"
#include "rng.h"

namespace {

// Fixed salts: which hash stream each field draws its seed and its picks from. Changing one
// changes every world, so treat them as part of the format.
constexpr uint32_t SALT_SURFACE = 0x51u;
constexpr uint32_t SALT_BAND = 0x52u;
constexpr uint32_t SALT_REGION = 0x53u;
constexpr uint32_t SALT_TUNNEL = 0x54u;
constexpr uint32_t SALT_CAVERN = 0x55u;
constexpr uint32_t SALT_POCKET = 0x56u;
constexpr uint32_t SALT_POOL = 0x57u;
constexpr uint32_t SALT_ENTRANCE = 0x58u;
constexpr uint32_t SALT_REGION_OFFSET = 0x59u;
constexpr uint32_t SALT_REGION_PICK = 0x5Au;
constexpr uint32_t SALT_BIOME_PICK = 0x5Bu;
constexpr uint32_t SALT_ROOM = 0x5Cu;
constexpr uint32_t SALT_ROOM_PICK = 0x5Du;
constexpr uint32_t SALT_ROOM_X = 0x5Eu;
constexpr uint32_t SALT_ROOM_Y = 0x5Fu;

// Sentinels in a room's resolved pixel table: the palette said keep the generated cell, or
// stamp a light marker (air plus an entry in Chunk::light_markers).
constexpr MatId MAT_KEEP = 0xFFFE;
constexpr MatId MAT_LIGHT_MARKER = 0xFFFD;

// Feature sizes the config has no field for: a band edge wobbles over ~500 cells, the
// region warp over ~1000.
constexpr float BAND_FREQUENCY = 0.002f;
constexpr float REGION_FREQUENCY = 0.001f;

double smoothstep01(double t) {
	if (t <= 0.0) return 0.0;
	if (t >= 1.0) return 1.0;
	return t * t * (3.0 - 2.0 * t);
}

// Share of the near side at signed distance `d` from a border spanning `width` cells.
double side_share(double d, double width) { return smoothstep01(d / width + 0.5); }

FastNoiseLite field_noise(uint32_t seed, uint32_t salt, float frequency, int octaves,
                          FastNoiseLite::FractalType fractal) {
	FastNoiseLite n;
	n.SetSeed((int)hash5(seed, 0, 0, 0, salt));
	n.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	n.SetFrequency(frequency);
	n.SetFractalType(fractal);
	n.SetFractalOctaves(octaves);
	return n;
}

bool resolve_material(const MaterialTable& table, const BiomeDef& biome, const std::string& name,
                      const char* field, MatId& out, std::string& error) {
	out = MAT_AIR;
	if (name.empty()) return true;
	out = table.id_of(name);
	if (out == NO_MAT) {
		error = "biome '" + biome.name + "': " + field + " names unknown material '" + name + "'";
		return false;
	}
	return true;
}

std::string colour_hex(uint32_t rgba) {
	char buf[10];
	snprintf(buf, sizeof buf, "#%08x", rgba);
	return buf;
}

}  // namespace

std::unique_ptr<TerrainGenerator> TerrainGenerator::compile(const TerrainConfig& cfg,
                                                            const MaterialTable& table,
                                                            uint32_t seed, std::string& error) {
	error.clear();
	if (cfg.biomes.empty()) {
		error = "terrain config has no biomes";
		return nullptr;
	}

	std::unique_ptr<TerrainGenerator> gen(new TerrainGenerator(cfg, table, seed));

	gen->ids_.resize(cfg.biomes.size());
	for (size_t i = 0; i < cfg.biomes.size(); ++i) {
		const BiomeDef& def = cfg.biomes[i];
		if (def.base_material.empty()) {
			error = "biome '" + def.name + "': base_material is required";
			return nullptr;
		}
		BiomeIds& ids = gen->ids_[i];
		if (!resolve_material(table, def, def.surface_material, "surface_material", ids.surface,
		                      error) ||
		    !resolve_material(table, def, def.base_material, "base_material", ids.base, error) ||
		    !resolve_material(table, def, def.pocket_material, "pocket_material", ids.pocket,
		                      error) ||
		    !resolve_material(table, def, def.liquid_material, "liquid_material", ids.liquid,
		                      error))
			return nullptr;
	}

	std::map<uint32_t, MatId> palette;  // rgba -> what a room pixel stamps
	for (const PaletteEntry& entry : cfg.palette) {
		MatId mat = MAT_AIR;  // markers stamp air
		if (entry.kind == PaletteKind::Material) {
			mat = table.id_of(entry.material);
			if (mat == NO_MAT) {
				error = "palette colour " + colour_hex(entry.rgba) + " names unknown material '" +
				        entry.material + "'";
				return nullptr;
			}
		} else if (entry.kind == PaletteKind::Keep) {
			mat = MAT_KEEP;
		} else if (entry.kind == PaletteKind::MarkerLight) {
			mat = MAT_LIGHT_MARKER;
		}
		palette.emplace(entry.rgba, mat);  // a duplicate colour keeps its first entry
	}

	if (cfg.room_cell_size <= 0 || cfg.room_cell_size % CHUNK != 0) {
		error = "room_cell_size must be a positive multiple of " + std::to_string(CHUNK) +
		        ", not " + std::to_string(cfg.room_cell_size);
		return nullptr;
	}
	gen->room_pixels_.resize(cfg.rooms.size());
	for (size_t i = 0; i < cfg.rooms.size(); ++i) {
		const RoomDef& def = cfg.rooms[i];
		if (def.width <= 0 || def.height <= 0 || def.width > cfg.room_cell_size ||
		    def.height > cfg.room_cell_size) {
			error = "room '" + def.name + "' is " + std::to_string(def.width) + "x" +
			        std::to_string(def.height) + ", which does not fit room_cell_size " +
			        std::to_string(cfg.room_cell_size);
			return nullptr;
		}
		for (const std::string& wanted : def.biomes) {
			bool known = false;
			for (const BiomeDef& other : cfg.biomes)
				if (other.name == wanted) known = true;
			if (!known) {
				error = "room '" + def.name + "': unknown biome '" + wanted + "'";
				return nullptr;
			}
		}
		const size_t count = (size_t)def.width * (size_t)def.height;
		if (def.rgba.size() != count) {
			error = "room '" + def.name + "': " + std::to_string(count) + " pixels expected, " +
			        std::to_string(def.rgba.size()) + " given";
			return nullptr;
		}
		std::vector<MatId>& cells = gen->room_pixels_[i];
		cells.resize(count);
		for (int y = 0; y < def.height; ++y)
			for (int x = 0; x < def.width; ++x) {
				const size_t cell = (size_t)y * def.width + x;
				const uint32_t rgba = def.rgba[cell];
				MatId mat = MAT_KEEP;  // alpha 0 is keep, before any palette lookup
				if ((rgba & 0xFFu) != 0) {
					auto it = palette.find(rgba);
					if (it == palette.end()) {
						error = "room '" + def.name + "': pixel (" + std::to_string(x) + ", " +
						        std::to_string(y) + ") has colour " + colour_hex(rgba) +
						        ", which is not in the palette";
						return nullptr;
					}
					mat = it->second;
				}
				cells[cell] = mat;
			}
	}

	// Group biomes by depth range, then order the bands by depth.
	for (size_t i = 0; i < cfg.biomes.size(); ++i) {
		const BiomeDef& def = cfg.biomes[i];
		auto it = std::find_if(gen->bands_.begin(), gen->bands_.end(), [&](const Band& band) {
			return band.min_depth == def.min_depth && band.max_depth == def.max_depth;
		});
		if (it == gen->bands_.end()) {
			Band band;
			band.min_depth = def.min_depth;
			band.max_depth = def.max_depth;
			band.biomes.push_back((int)i);
			gen->bands_.push_back(band);
		} else {
			it->biomes.push_back((int)i);
		}
	}
	std::sort(gen->bands_.begin(), gen->bands_.end(),
	          [](const Band& a, const Band& b) { return a.min_depth < b.min_depth; });

	const Band& first = gen->bands_.front();
	if (first.min_depth != 0) {
		error = "first band must start at depth 0, not at " + std::to_string(first.min_depth);
		return nullptr;
	}
	for (size_t i = 1; i < gen->bands_.size(); ++i) {
		if (gen->bands_[i].min_depth != gen->bands_[i - 1].max_depth) {
			error = "band gap between depth " + std::to_string(gen->bands_[i - 1].max_depth) +
			        " and depth " + std::to_string(gen->bands_[i].min_depth);
			return nullptr;
		}
	}
	const Band& last = gen->bands_.back();
	if (last.max_depth != -1) {
		error = "the last band must be open: max_depth -1, not " +
		        std::to_string(last.max_depth);
		return nullptr;
	}

	if (cfg.region_width <= 0) {
		error = "region_width must be positive, not " + std::to_string(cfg.region_width);
		return nullptr;
	}
	if (cfg.region_transition > cfg.region_width / 2) {
		error = "region_transition " + std::to_string(cfg.region_transition) +
		        " is more than half region_width " + std::to_string(cfg.region_width);
		return nullptr;
	}
	int thinnest = INT_MAX;
	for (const Band& band : gen->bands_)
		if (band.max_depth >= 0) thinnest = std::min(thinnest, band.max_depth - band.min_depth);
	if (thinnest != INT_MAX && cfg.band_transition > thinnest / 2) {
		error = "band_transition " + std::to_string(cfg.band_transition) +
		        " is more than half the thinnest band (" + std::to_string(thinnest) + ")";
		return nullptr;
	}

	gen->region_offset_.resize(gen->bands_.size());
	for (size_t band = 0; band < gen->bands_.size(); ++band) {
		gen->region_offset_[band] =
		    (int)(hash5(seed, 0, (int32_t)band, 0, SALT_REGION_OFFSET) % (uint32_t)cfg.region_width);
	}

	gen->n_surface_ =
	    field_noise(seed, SALT_SURFACE, cfg.surface_frequency, 4, FastNoiseLite::FractalType_FBm);
	gen->n_band_ = field_noise(seed, SALT_BAND, BAND_FREQUENCY, 2, FastNoiseLite::FractalType_FBm);
	gen->n_region_ =
	    field_noise(seed, SALT_REGION, REGION_FREQUENCY, 1, FastNoiseLite::FractalType_None);
	// Ridged creases give long winding tunnels; plain fBm gives blobby caverns.
	gen->n_tunnel_ =
	    field_noise(seed, SALT_TUNNEL, cfg.tunnel_frequency, 2, FastNoiseLite::FractalType_Ridged);
	gen->n_cavern_ =
	    field_noise(seed, SALT_CAVERN, cfg.cavern_frequency, 2, FastNoiseLite::FractalType_FBm);
	gen->n_pocket_ =
	    field_noise(seed, SALT_POCKET, cfg.pocket_frequency, 2, FastNoiseLite::FractalType_FBm);
	gen->n_pool_ =
	    field_noise(seed, SALT_POOL, cfg.pool_frequency, 2, FastNoiseLite::FractalType_FBm);
	gen->n_entrance_ =
	    field_noise(seed, SALT_ENTRANCE, cfg.entrance_frequency, 1, FastNoiseLite::FractalType_None);

	return gen;
}

int TerrainGenerator::surface_height(int wx) const {
	const double n = n_surface_.GetNoise((double)wx, 0.0);
	return config_.surface_y + (int)std::lround((double)config_.surface_amplitude * n);
}

double TerrainGenerator::region_x(int wx) const {
	if (config_.region_warp == 0) return (double)wx;
	return (double)wx + (double)config_.region_warp * (double)n_region_.GetNoise((double)wx, 0.0);
}

int TerrainGenerator::band_index(double warped_depth) const {
	for (size_t i = 0; i < bands_.size(); ++i) {
		if (bands_[i].max_depth < 0 || warped_depth < (double)bands_[i].max_depth) return (int)i;
	}
	return (int)bands_.size() - 1;
}

int TerrainGenerator::pick_biome(int band, int region) const {
	const std::vector<int>& candidates = bands_[band].biomes;
	double total = 0;
	for (int biome : candidates) total += (double)config_.biomes[biome].weight;
	if (total <= 0) return candidates.front();
	double target = (double)hash_unit(hash5(seed_, 0, band, region, SALT_REGION_PICK)) * total;
	double acc = 0;
	for (int biome : candidates) {
		acc += (double)config_.biomes[biome].weight;
		if (target < acc) return biome;
	}
	return candidates.back();
}

BiomeWeights TerrainGenerator::weights_at(int wx, int wy) const {
	const int depth = wy - surface_height(wx);
	if (depth < 0) return BiomeWeights();
	return weights_col(wx, region_x(wx), wy, depth);
}

BiomeWeights TerrainGenerator::weights_col(int wx, double xw, int wy, int depth) const {
	double warped = (double)depth;
	if (config_.band_warp != 0)
		warped += (double)config_.band_warp * (double)n_band_.GetNoise((double)wx, (double)wy);

	const int band = band_index(warped);
	double band_self = 1.0;
	double band_low = 0.0, band_high = 0.0;
	int band_below = -1, band_above = -1;
	if (config_.band_transition > 0) {
		const double width = (double)config_.band_transition;
		const double half = 0.5 * width;
		if (band > 0) {
			const double d = warped - (double)bands_[band].min_depth;
			if (d < half) {
				const double share = side_share(d, width);
				band_self *= share;
				band_low = 1.0 - share;
				band_below = band - 1;
			}
		}
		if (band + 1 < (int)bands_.size()) {
			const double d = (double)bands_[band].max_depth - warped;
			if (d < half) {
				const double share = side_share(d, width);
				band_self *= share;
				band_high = 1.0 - share;
				band_above = band + 1;
			}
		}
	}

	int found[4];
	double weight[4];
	int count = 0;
	auto add = [&](int biome, double w) {
		if (w <= 0.0) return;
		for (int i = 0; i < count; ++i) {
			if (found[i] == biome) {
				weight[i] += w;
				return;
			}
		}
		if (count == 4) return;  // unreachable: at most two bands, two regions each
		found[count] = biome;
		weight[count] = w;
		++count;
	};
	// The x weights inside one band, multiplied by that band's depth weight.
	auto add_band = [&](int band_i, double band_weight) {
		const int offset = region_offset_[band_i];
		const int region =
		    floor_div((int)std::floor(xw) + offset, config_.region_width);
		const double left_edge = (double)region * config_.region_width - offset;
		const double right_edge = left_edge + config_.region_width;
		double self = 1.0, low = 0.0, high = 0.0;
		int region_below = -1, region_above = -1;
		if (config_.region_transition > 0) {
			const double width = (double)config_.region_transition;
			const double half = 0.5 * width;
			if (xw - left_edge < half) {
				const double share = side_share(xw - left_edge, width);
				self *= share;
				low = 1.0 - share;
				region_below = region - 1;
			}
			if (right_edge - xw < half) {
				const double share = side_share(right_edge - xw, width);
				self *= share;
				high = 1.0 - share;
				region_above = region + 1;
			}
		}
		add(pick_biome(band_i, region), band_weight * self);
		if (region_below >= 0) add(pick_biome(band_i, region_below), band_weight * low);
		if (region_above >= 0) add(pick_biome(band_i, region_above), band_weight * high);
	};

	add_band(band, band_self);
	if (band_below >= 0) add_band(band_below, band_low);
	if (band_above >= 0) add_band(band_above, band_high);

	BiomeWeights out;
	double total = 0;
	for (int i = 0; i < count; ++i) total += weight[i];
	if (total <= 0) return out;
	for (int i = 0; i < count; ++i) {
		out.biome[i] = found[i];
		out.weight[i] = (float)(weight[i] / total);
	}
	out.count = count;
	return out;
}

TerrainGenerator::Blended TerrainGenerator::blend(const BiomeWeights& w) const {
	Blended out;
	if (w.count == 0) return out;
	if (w.count == 1) {
		const BiomeDef& def = config_.biomes[w.biome[0]];
		out.tunnel = def.tunnel_threshold;
		out.cavern = def.cavern_threshold;
		out.pocket = def.pocket_threshold;
		out.liquid = def.liquid_threshold;
		out.surface_depth = def.surface_depth;
		return out;
	}
	double total = 0;
	for (int i = 0; i < w.count; ++i) total += (double)w.weight[i];
	if (total <= 0) return out;
	for (int i = 0; i < w.count; ++i) {
		const BiomeDef& def = config_.biomes[w.biome[i]];
		const double k = (double)w.weight[i] / total;
		out.tunnel += k * def.tunnel_threshold;
		out.cavern += k * def.cavern_threshold;
		out.pocket += k * def.pocket_threshold;
		out.liquid += k * def.liquid_threshold;
		out.surface_depth += k * def.surface_depth;
	}
	return out;
}

int TerrainGenerator::pick_from(const BiomeWeights& w, int wx, int wy) const {
	if (w.count <= 0) return -1;
	if (w.count == 1) return w.biome[0];
	const double u = (double)hash_unit(hash5(seed_, 0, wx, wy, SALT_BIOME_PICK));
	double acc = 0;
	for (int i = 0; i < w.count; ++i) {
		acc += (double)w.weight[i];
		if (u < acc) return w.biome[i];
	}
	return w.biome[w.count - 1];
}

int TerrainGenerator::biome_at(int wx, int wy) const {
	return pick_from(weights_at(wx, wy), wx, wy);
}

double TerrainGenerator::taper_at(int wx, int depth) const {
	double taper = 1.0;
	if (config_.cave_taper_depth > 0) {
		taper = (double)depth / (double)config_.cave_taper_depth;
		if (taper > 1.0) taper = 1.0;
		if (taper < 0.0) taper = 0.0;
	}
	if (taper < 1.0 &&
	    (double)n_entrance_.GetNoise((double)wx, 0.0) > (double)config_.entrance_threshold)
		taper = 1.0;
	return taper;
}

bool TerrainGenerator::cave_for(int wx, int wy, int depth, const Blended& amounts) const {
	const double taper = taper_at(wx, depth);
	if (taper <= 0.0) return false;
	if (amounts.tunnel > 0.0 &&
	    std::fabs((double)n_tunnel_.GetNoise((double)wx, (double)wy * (double)config_.cave_stretch)) <
	        amounts.tunnel * taper)
		return true;
	if (amounts.cavern > 0.0 &&
	    (double)n_cavern_.GetNoise((double)wx, (double)wy) > 1.0 - 2.0 * amounts.cavern * taper)
		return true;
	return false;
}

bool TerrainGenerator::cave_at(int wx, int wy) const {
	const int depth = wy - surface_height(wx);
	if (depth < 0) return false;
	const BiomeWeights w = weights_col(wx, region_x(wx), wy, depth);
	return cave_for(wx, wy, depth, blend(w));
}

std::optional<RoomPlacement> TerrainGenerator::room_in_cell(int px, int py) const {
	const int size = config_.room_cell_size;
	if (config_.rooms.empty() ||
	    !(hash_unit(hash5(seed_, 0, px, py, SALT_ROOM)) < config_.room_chance))
		return std::nullopt;

	// Highest-weight biome at the cell centre; above the surface only rooms with no biome fit.
	const BiomeWeights weights = weights_at(px * size + size / 2, py * size + size / 2);
	int top = -1;
	for (int i = 0; i < weights.count; ++i)
		if (top < 0 || weights.weight[i] > weights.weight[top]) top = i;
	const std::string* top_name = top >= 0 ? &config_.biomes[weights.biome[top]].name : nullptr;

	std::vector<int> candidates;
	double total = 0;
	for (size_t i = 0; i < config_.rooms.size(); ++i) {
		const RoomDef& def = config_.rooms[i];
		bool eligible = def.biomes.empty();
		for (const std::string& name : def.biomes)
			if (top_name != nullptr && name == *top_name) eligible = true;
		if (!eligible) continue;
		candidates.push_back((int)i);
		total += (double)def.weight;
	}
	if (candidates.empty() || total <= 0) return std::nullopt;

	const double target = (double)hash_unit(hash5(seed_, 0, px, py, SALT_ROOM_PICK)) * total;
	int room = candidates.back();
	double acc = 0;
	for (int candidate : candidates) {
		acc += (double)config_.rooms[candidate].weight;
		if (target < acc) {
			room = candidate;
			break;
		}
	}

	const RoomDef& def = config_.rooms[room];
	const int span_x = size - def.width, span_y = size - def.height;
	const int off_x =
	    span_x > 0 ? (int)(hash5(seed_, 0, px, py, SALT_ROOM_X) % (uint32_t)(span_x + 1)) : 0;
	const int off_y =
	    span_y > 0 ? (int)(hash5(seed_, 0, px, py, SALT_ROOM_Y) % (uint32_t)(span_y + 1)) : 0;
	const int x = px * size + off_x;
	int y = py * size + off_y;

	switch (def.anchor) {
		case RoomAnchor::Surface: {
			const int top_y = surface_height(x + def.width / 2) - def.height + 1;
			if (top_y < py * size || top_y + def.height > (py + 1) * size) return std::nullopt;
			y = top_y;
			break;
		}
		case RoomAnchor::Embedded: {
			int highest = INT_MIN;
			for (int i = 0; i < def.width; ++i) highest = std::max(highest, surface_height(x + i));
			if (!(y > highest)) return std::nullopt;
			break;
		}
		case RoomAnchor::Cave:
			if (!cave_at(x + def.width / 2, y + def.height / 2)) return std::nullopt;
			break;
	}
	return RoomPlacement{room, x, y};
}

void TerrainGenerator::generate(ChunkCoord coord, Chunk& out, uint32_t seed) const {
	(void)seed;  // every field was seeded by compile(); a different seed needs a new generator
	out.light_markers.clear();
	const int base_x = coord.x * CHUNK;
	const int base_y = coord.y * CHUNK;
	// room_cell_size is a multiple of CHUNK, so a chunk lies in exactly one placement cell.
	std::optional<RoomPlacement> room;
	if (!config_.rooms.empty())
		room = room_in_cell(floor_div(base_x, config_.room_cell_size),
		                    floor_div(base_y, config_.room_cell_size));
	const RoomDef* room_def = room ? &config_.rooms[room->room] : nullptr;
	const std::vector<MatId>* room_cells = room ? &room_pixels_[room->room] : nullptr;
	for (int lx = 0; lx < CHUNK; ++lx) {
		const int wx = base_x + lx;
		const int h = surface_height(wx);
		const double xw = region_x(wx);
		for (int ly = 0; ly < CHUNK; ++ly) {
			const int wy = base_y + ly;
			const int depth = wy - h;
			MatId mat = MAT_AIR;
			if (depth >= 0) {
				// shortcut: every cell re-computes the biome weights; the plan's single-biome
				// column fast path is deferred. CPU only, generation runs in the background.
				const BiomeWeights weights = weights_col(wx, xw, wy, depth);
				const Blended amounts = blend(weights);
				const int picked = pick_from(weights, wx, wy);
				const BiomeIds& ids = ids_[picked];
				const int top = (int)std::lround(amounts.surface_depth);
				if (ids.surface != MAT_AIR && depth < top) {
					mat = ids.surface;
				} else if (ids.pocket != MAT_AIR && amounts.pocket > 0.0 &&
				           (double)n_pocket_.GetNoise((double)wx, (double)wy) >
				               1.0 - 2.0 * amounts.pocket) {
					mat = ids.pocket;
				} else {
					mat = ids.base;
				}
				if (cave_for(wx, wy, depth, amounts)) {
					mat = MAT_AIR;
					if (ids.liquid != MAT_AIR && amounts.liquid > 0.0 &&
					    (double)n_pool_.GetNoise((double)wx, (double)wy) >
					        1.0 - 2.0 * amounts.liquid)
						mat = ids.liquid;
				}
			}
			if (room_def != nullptr) {
				const int rx = wx - room->x, ry = wy - room->y;
				if (rx >= 0 && ry >= 0 && rx < room_def->width && ry < room_def->height) {
					const MatId stamped = (*room_cells)[(size_t)ry * room_def->width + rx];
					if (stamped != MAT_KEEP) mat = stamped;
				}
			}
			if (mat == MAT_LIGHT_MARKER) {
				mat = MAT_AIR;
				out.light_markers.push_back((uint16_t)(ly * CHUNK + lx));
			}
			out.at(lx, ly) = spawn_cell(mat, table_, seed_, wx, wy);
		}
	}
}
