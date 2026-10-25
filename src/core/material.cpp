#include "material.h"

namespace {

CompiledMaterial compiled_from(const MaterialDef& d, MatId burns_into, MatId high_into,
                               MatId low_into, MatId decays_into) {
	CompiledMaterial c;
	c.name = d.name;
	c.cls = d.cls;
	c.density = d.density;
	c.color_rgba = d.color_rgba;
	c.shade_amount = d.shade_amount;
	c.dispersion = d.dispersion;
	c.emission_r = d.emission_r;
	c.emission_g = d.emission_g;
	c.emission_b = d.emission_b;
	c.emission_energy = d.emission_energy;
	c.light_opacity = d.light_opacity;
	c.collision_solid = d.collision_solid;
	c.conductivity = d.conductivity;
	c.fixed_temp = d.fixed_temp;
	c.base_temp = d.base_temp;
	c.ignite_temp = d.ignite_temp;
	c.burn_life = d.burn_life;
	c.burn_heat = d.burn_heat;
	c.burns_into = burns_into;
	c.high_temp = d.high_temp;
	c.high_into = high_into;
	c.low_temp = d.low_temp;
	c.low_into = low_into;
	c.lifetime = d.lifetime;
	c.decays_into = decays_into;
	c.has_reactions = false;
	return c;
}

// Empty name = "no such transition" (NO_MAT); a name must exist.
bool resolve(const std::string& owner, const char* field, const std::string& ref,
             const MaterialTable& table, MatId& id, std::string& error) {
	if (ref.empty()) {
		id = NO_MAT;
		return true;
	}
	id = table.id_of(ref);
	if (id == NO_MAT) {
		error = owner + "." + field + ": unknown material '" + ref + "'";
		return false;
	}
	return true;
}

}  // namespace

MatId MaterialTable::id_of(std::string_view name) const { return lookup(name); }

MatId MaterialTable::lookup(std::string_view name) const {
	for (size_t i = 0; i < mats.size(); ++i)
		if (mats[i].name == name) return (MatId)i;
	return NO_MAT;
}

const CompiledMaterial& MaterialTable::get(MatId id) const {
	static const CompiledMaterial kNone{};
	if (mats.empty()) return kNone;
	return mats[id < mats.size() ? id : 0];  // unknown id reads as air rather than UB
}

const std::string& MaterialTable::name_of(MatId id) const {
	static const std::string kNone;
	return id < mats.size() ? mats[id].name : kNone;
}

const Reaction* MaterialTable::reaction(MatId self, MatId other) const {
	const size_t n = mats.size();
	if (self >= n || other >= n) return nullptr;
	const int32_t idx = rxn_index[self * n + other];
	return idx < 0 ? nullptr : &rxs[(size_t)idx];
}

bool MaterialTable::compile(const std::vector<MaterialDef>& defs,
                            const std::vector<ReactionDef>& reactions, MaterialTable& out,
                            std::string& error) {
	out = MaterialTable{};
	error.clear();

	MaterialDef air;
	air.name = "air";
	air.cls = MatClass::Empty;
	air.density = 0.0f;
	air.color_rgba = 0x00000000;
	out.mats.push_back(compiled_from(air, NO_MAT, NO_MAT, NO_MAT, NO_MAT));

	for (const MaterialDef& d : defs) {
		if (d.name.empty()) {
			error = "material at index " + std::to_string(out.mats.size() - 1) + " has no name";
			return false;
		}
		if (out.lookup(d.name) != NO_MAT) {
			error = "duplicate material '" + d.name + "'";
			return false;
		}
		out.mats.push_back(compiled_from(d, NO_MAT, NO_MAT, NO_MAT, NO_MAT));
	}

	// Second pass: defs may reference materials declared after them, so resolve now
	// that every name is known.
	for (size_t i = 0; i < defs.size(); ++i) {
		const MaterialDef& d = defs[i];
		CompiledMaterial& c = out.mats[i + 1];
		if (!resolve(d.name, "burns_into", d.burns_into, out, c.burns_into, error)) return false;
		if (!resolve(d.name, "high_into", d.high_into, out, c.high_into, error)) return false;
		if (!resolve(d.name, "low_into", d.low_into, out, c.low_into, error)) return false;
		if (!resolve(d.name, "decays_into", d.decays_into, out, c.decays_into, error)) return false;
	}

	// Dense pair matrix, both orientations written now so the sim never branches on order.
	const size_t n = out.mats.size();
	out.rxn_index.assign(n * n, -1);
	for (const ReactionDef& r : reactions) {
		const MatId a = out.id_of(r.a);
		const MatId b = out.id_of(r.b);
		const std::string pair = "reaction " + r.a + " + " + r.b;
		if (a == NO_MAT) {
			error = pair + ": unknown material '" + r.a + "'";
			return false;
		}
		if (b == NO_MAT) {
			error = pair + ": unknown material '" + r.b + "'";
			return false;
		}
		if (out.rxn_index[a * n + b] >= 0) {
			error = pair + ": duplicate pair";
			return false;
		}
		Reaction fwd;
		fwd.probability = r.probability;
		fwd.min_temp = r.min_temp;
		fwd.max_temp = r.max_temp;
		if (!resolve(pair, "a_into", r.a_into, out, fwd.a_into, error)) return false;
		if (!resolve(pair, "b_into", r.b_into, out, fwd.b_into, error)) return false;
		out.rxs.push_back(fwd);
		out.rxn_index[a * n + b] = (int32_t)(out.rxs.size() - 1);
		if (a != b) {
			Reaction rev;
			rev.a_into = fwd.b_into;
			rev.b_into = fwd.a_into;
			rev.probability = fwd.probability;
			rev.min_temp = fwd.min_temp;
			rev.max_temp = fwd.max_temp;
			out.rxs.push_back(rev);
			out.rxn_index[b * n + a] = (int32_t)(out.rxs.size() - 1);
		}
		out.mats[a].has_reactions = true;
		out.mats[b].has_reactions = true;
	}
	return true;
}
