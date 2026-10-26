#pragma once
#include <cmath>
#include <string>
#include <vector>

#include "box2d/box2d.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "generator.h"
#include "material.h"
#include "physics.h"
#include "simulator.h"
#include "world_grid.h"

// Shared by the physics tests: a material set, an air arena with a rock border, painting and
// body helpers, and the per-tick physics loop.
namespace phystest {

// air(implicit) + rock, sand (collision-solid powder), water, wood (light solid), acid
// (acid + rock -> air + air, always).
inline MaterialTable table() {
	std::vector<MaterialDef> defs(5);
	defs[0].name = "rock";
	defs[0].cls = MatClass::Solid;
	defs[0].density = 3.0f;
	defs[0].collision_solid = true;
	defs[1].name = "sand";
	defs[1].cls = MatClass::Powder;
	defs[1].density = 1.6f;
	defs[1].collision_solid = true;
	defs[2].name = "water";
	defs[2].cls = MatClass::Liquid;
	defs[2].density = 1.0f;
	defs[2].dispersion = 5;
	defs[3].name = "wood";
	defs[3].cls = MatClass::Solid;
	defs[3].density = 0.5f;
	defs[3].collision_solid = true;
	defs[4].name = "acid";
	defs[4].cls = MatClass::Liquid;
	defs[4].density = 1.1f;
	ReactionDef acid_rock;
	acid_rock.a = "acid";
	acid_rock.b = "rock";
	acid_rock.a_into = "air";
	acid_rock.b_into = "air";
	acid_rock.probability = 1.0f;
	MaterialTable t;
	std::string error;
	REQUIRE(MaterialTable::compile(defs, {acid_rock}, t, error));
	return t;
}

// Loads the chunks covering [x0,x1) x [y0,y1): air inside, rock outside it.
inline void arena(WorldGrid& grid, const MaterialTable& t, int x0, int y0, int x1, int y1) {
	BoxGenerator gen(x0, y0, x1, y1, t.id_of("rock"), t);
	for (int cy = floor_div(y0, CHUNK); cy <= floor_div(y1 - 1, CHUNK); ++cy)
		for (int cx = floor_div(x0, CHUNK); cx <= floor_div(x1 - 1, CHUNK); ++cx)
			REQUIRE(grid.load({cx, cy}, gen) != nullptr);
}

inline void fill(WorldGrid& grid, int x0, int y0, int x1, int y1, MatId mat) {
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x) REQUIRE(grid.set(x, y, mat));
}

inline BodyDef box(const MaterialTable& t, const char* mat, int w, int h, float x, float y,
                   BodyShape shape = BodyShape::Hull) {
	BodyDef d;
	d.w = w;
	d.h = h;
	d.pixels.assign((size_t)w * h, t.id_of(mat));
	d.shape = shape;
	d.x = x;
	d.y = y;
	return d;
}

inline BodyDef disc(const MaterialTable& t, const char* mat, int radius, float x, float y) {
	const int size = radius * 2;
	BodyDef d = box(t, mat, size, size, x, y, BodyShape::Circle);
	for (int py = 0; py < size; ++py)
		for (int px = 0; px < size; ++px) {
			const float dx = px + 0.5f - radius, dy = py + 0.5f - radius;
			if (dx * dx + dy * dy > (float)radius * radius) d.pixels[(size_t)py * size + px] = NO_MAT;
		}
	return d;
}

inline void run(PhysicsWorld& physics, int ticks) {
	for (int i = 0; i < ticks; ++i) {
		physics.update_terrain();
		physics.step();
	}
}

inline b2Vec2 pos(const PhysicsWorld& physics, int handle) { return b2Body_GetPosition(physics.body_id(handle)); }
inline float angle(const PhysicsWorld& physics, int handle) {
	return b2Rot_GetAngle(b2Body_GetRotation(physics.body_id(handle)));
}

}  // namespace phystest
