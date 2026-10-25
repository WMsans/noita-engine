#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cell.h"

enum class MatClass : uint8_t { Empty, Solid, Powder, Liquid, Gas };

constexpr int16_t NO_TEMP = INT16_MAX;
constexpr MatId NO_MAT = 0xFFFF;

// Authored material, as written by hand or loaded from data files. Every field left
// unset means "not reactive / no special behaviour" (see the defaults below).
struct MaterialDef {
	std::string name;
	MatClass cls = MatClass::Empty;
	float density = 1.0f;
	uint32_t color_rgba = 0xFFFFFFFF;
	float shade_amount = 0.1f;  // brightness jitter 0..1
	uint8_t dispersion = 0;     // sideways cells/tick, liquid/gas, <= 16
	float emission_r = 0, emission_g = 0, emission_b = 0, emission_energy = 0;
	float light_opacity = 1.0f;
	bool collision_solid = false;
	float conductivity = 0.1f;
	bool fixed_temp = false;
	int16_t base_temp = AMBIENT_TEMP;
	int16_t ignite_temp = NO_TEMP;
	uint8_t burn_life = 0;
	int16_t burn_heat = 0;
	std::string burns_into;
	int16_t high_temp = NO_TEMP;
	std::string high_into;
	int16_t low_temp = NO_TEMP;
	std::string low_into;
	uint8_t lifetime = 0;  // 0 = permanent
	std::string decays_into;
};

struct ReactionDef {
	std::string a, b, a_into, b_into;
	float probability;
	int16_t min_temp = -273, max_temp = 5000;
};

// MaterialDef with every *_into name resolved to a MatId (NO_MAT when unset).
struct CompiledMaterial {
	std::string name;  // setup-time only, never touched by the sim loop
	MatClass cls;
	float density;
	uint32_t color_rgba;
	float shade_amount;
	uint8_t dispersion;
	float emission_r, emission_g, emission_b, emission_energy, light_opacity;
	bool collision_solid;
	float conductivity;
	bool fixed_temp;
	int16_t base_temp;
	int16_t ignite_temp;
	uint8_t burn_life;
	int16_t burn_heat;
	MatId burns_into;
	int16_t high_temp;
	MatId high_into;
	int16_t low_temp;
	MatId low_into;
	uint8_t lifetime;
	MatId decays_into;
	bool has_reactions;
};

// One orientation of a reaction pair: a_into applies to `a`, b_into to `b`.
struct Reaction {
	MatId a_into, b_into;
	float probability;
	int16_t min_temp, max_temp;
};

class MaterialTable {
public:
	// Air (id 0, Empty, density 0) is implicit; defs get ids 1..n in order.
	// Returns false and fills `error` on an unknown *_into name or a duplicate pair.
	static bool compile(const std::vector<MaterialDef>& defs,
	                    const std::vector<ReactionDef>& reactions, MaterialTable& out,
	                    std::string& error);

	MatId id_of(std::string_view name) const;  // NO_MAT if unknown
	const CompiledMaterial& get(MatId id) const;
	const std::string& name_of(MatId id) const;
	// Null when the pair has no reaction. Returned orientation has a_into for `self`.
	const Reaction* reaction(MatId self, MatId other) const;
	size_t size() const { return mats.size(); }

private:
	MatId lookup(std::string_view name) const;

	std::vector<CompiledMaterial> mats;  // index = MatId, 0 = air
	std::vector<Reaction> rxs;           // both orientations of every pair
	std::vector<int32_t> rxn_index;      // size n*n, index into rxs, -1 = none
};
