#include <string>
#include <utility>
#include <vector>

#include "doctest/doctest.h"
#include "material.h"

namespace {

MaterialDef make_def(const std::string& name, MatClass cls) {
	MaterialDef d;
	d.name = name;
	d.cls = cls;
	return d;
}

// sand (powder), water (liquid, boils at 100 -> steam), steam (gas, lifetime 200 -> water)
std::vector<MaterialDef> sand_water_steam() {
	MaterialDef sand = make_def("sand", MatClass::Powder);
	MaterialDef water = make_def("water", MatClass::Liquid);
	water.high_temp = 100;
	water.high_into = "steam";
	MaterialDef steam = make_def("steam", MatClass::Gas);
	steam.lifetime = 200;
	steam.decays_into = "water";
	return {sand, water, steam};
}

bool has(const std::string& haystack, const std::string& needle) {
	return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("compile assigns ids and resolves references") {
	MaterialTable table;
	std::string error;
	REQUIRE(MaterialTable::compile(sand_water_steam(), {}, table, error));
	CHECK(error.empty());

	// air is implicit at id 0, defs follow in order
	CHECK(table.size() == 4);
	CHECK(table.id_of("air") == MAT_AIR);
	CHECK(table.id_of("sand") == 1);
	CHECK(table.id_of("water") == 2);
	CHECK(table.id_of("steam") == 3);
	CHECK(table.id_of("nothing") == NO_MAT);

	CHECK(table.get(0).cls == MatClass::Empty);
	CHECK(table.get(0).density == 0.0f);
	CHECK(table.get(1).cls == MatClass::Powder);
	CHECK(table.name_of(3) == "steam");

	// names resolved to ids, missing ones to NO_MAT
	CHECK(table.get(2).high_temp == 100);
	CHECK(table.get(2).high_into == 3);
	CHECK(table.get(3).lifetime == 200);
	CHECK(table.get(3).decays_into == 2);
	CHECK(table.get(1).high_into == NO_MAT);
	CHECK(table.get(1).decays_into == NO_MAT);

	// defaults for unset fields
	const CompiledMaterial& sand = table.get(1);
	CHECK(sand.density == 1.0f);
	CHECK(sand.shade_amount == 0.1f);
	CHECK(sand.dispersion == 0);
	CHECK(sand.light_opacity == 1.0f);
	CHECK(sand.collision_solid == false);
	CHECK(sand.conductivity == 0.1f);
	CHECK(sand.fixed_temp == false);
	CHECK(sand.base_temp == 20);
	CHECK(sand.ignite_temp == NO_TEMP);
	CHECK(sand.burn_life == 0);
	CHECK(sand.burn_heat == 0);
	CHECK(sand.burns_into == NO_MAT);
	CHECK(sand.lifetime == 0);
	CHECK(sand.has_reactions == false);
}

TEST_CASE("unknown reference fails with the name in the error") {
	MaterialDef wood = make_def("wood", MatClass::Solid);
	wood.ignite_temp = 300;
	wood.burn_life = 100;
	wood.burns_into = "ash";  // no ash defined

	MaterialTable table;
	std::string error;
	CHECK_FALSE(MaterialTable::compile({wood}, {}, table, error));
	CHECK(has(error, "wood"));
	CHECK(has(error, "burns_into"));
	CHECK(has(error, "ash"));
}

TEST_CASE("reaction lookup is symmetric with oriented outputs") {
	std::vector<MaterialDef> defs{make_def("water", MatClass::Liquid),
	                              make_def("lava", MatClass::Liquid),
	                              make_def("steam", MatClass::Gas),
	                              make_def("rock", MatClass::Solid)};
	ReactionDef r;
	r.a = "water";
	r.b = "lava";
	r.a_into = "steam";
	r.b_into = "rock";
	r.probability = 1.0f;

	MaterialTable table;
	std::string error;
	REQUIRE(MaterialTable::compile(defs, {r}, table, error));

	CHECK(table.get(1).has_reactions);   // water
	CHECK(table.get(2).has_reactions);   // lava
	CHECK_FALSE(table.get(3).has_reactions);

	const Reaction* fwd = table.reaction(1, 2);  // water, lava
	REQUIRE(fwd != nullptr);
	CHECK(fwd->a_into == 3);  // water -> steam
	CHECK(fwd->b_into == 4);  // lava -> rock
	CHECK(fwd->probability == 1.0f);
	CHECK(fwd->min_temp == -273);
	CHECK(fwd->max_temp == 5000);

	const Reaction* rev = table.reaction(2, 1);  // lava, water
	REQUIRE(rev != nullptr);
	CHECK(rev->a_into == 4);  // lava -> rock
	CHECK(rev->b_into == 3);  // water -> steam

	CHECK(table.reaction(1, 3) == nullptr);  // no pair
	CHECK(table.reaction(1, 1) == nullptr);
}

TEST_CASE("duplicate reaction pair is an error") {	std::vector<MaterialDef> defs{make_def("water", MatClass::Liquid),
	                              make_def("lava", MatClass::Liquid)};
	ReactionDef first;
	first.a = "water";
	first.b = "lava";
	first.a_into = "water";
	first.b_into = "lava";
	first.probability = 1.0f;
	ReactionDef dup = first;
	std::swap(dup.a, dup.b);  // same pair, other order
	std::swap(dup.a_into, dup.b_into);

	MaterialTable table;
	std::string error;
	CHECK_FALSE(MaterialTable::compile(defs, {first, dup}, table, error));
	CHECK(has(error, "water"));
	CHECK(has(error, "lava"));
}

TEST_CASE("failed compile leaves the previous table intact") {
	std::vector<MaterialDef> defs{make_def("water", MatClass::Liquid),
	                              make_def("lava", MatClass::Liquid),
	                              make_def("steam", MatClass::Gas),
	                              make_def("rock", MatClass::Solid)};
	ReactionDef r;
	r.a = "water";
	r.b = "lava";
	r.a_into = "steam";
	r.b_into = "rock";
	r.probability = 1.0f;

	MaterialTable table;
	std::string error;
	REQUIRE(MaterialTable::compile(defs, {r}, table, error));

	MaterialDef wood = make_def("wood", MatClass::Solid);
	wood.burns_into = "ash";  // no ash defined: this compile fails
	CHECK_FALSE(MaterialTable::compile({wood}, {}, table, error));
	CHECK(has(error, "ash"));

	// the previous table must still be the good one
	CHECK(table.size() == 5);
	CHECK(table.id_of("water") == 1);
	const Reaction* fwd = table.reaction(1, 2);  // water, lava
	REQUIRE(fwd != nullptr);
	CHECK(fwd->a_into == 3);  // water -> steam
	CHECK(fwd->b_into == 4);  // lava -> rock

	// a table that never compiled is never usable
	CHECK(MaterialTable{}.reaction(0, 0) == nullptr);
}
