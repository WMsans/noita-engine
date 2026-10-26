#include "physics.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "creature.h"
#include "generator.h"
#include "material.h"
#include "world_grid.h"

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void* handle_data(int handle) { return reinterpret_cast<void*>((intptr_t)handle + 1); }
int handle_of(b2BodyId id) { return (int)(reinterpret_cast<intptr_t>(b2Body_GetUserData(id)) - 1); }

b2ShapeDef body_shape_def(const BodyDef& def, float density) {
	b2ShapeDef sd = b2DefaultShapeDef();
	sd.density = density;
	sd.material.friction = def.friction;
	sd.material.restitution = def.restitution;
	sd.filter.categoryBits = CAT_BODY;
	sd.filter.maskBits = CAT_TERRAIN | CAT_BODY | CAT_CREATURE;
	sd.enableHitEvents = true;
	return sd;
}

float polygon_area(const Loop& loop) {
	float a = 0;
	for (size_t i = 0; i < loop.size(); ++i) {
		const b2Vec2 p = loop[i], q = loop[(i + 1) % loop.size()];
		a += p.x * q.y - q.x * p.y;
	}
	return std::fabs(a) * 0.5f;
}

// The chunk range covering an AABB grown by `margin` cells.
ChunkRect chunks_around(b2AABB box, int margin) {
	const ChunkCoord lo = chunk_of((int)std::floor(box.lowerBound.x) - margin, (int)std::floor(box.lowerBound.y) - margin);
	const ChunkCoord hi = chunk_of((int)std::floor(box.upperBound.x) + margin, (int)std::floor(box.upperBound.y) + margin);
	return {lo.x, lo.y, hi.x, hi.y};
}

}  // namespace

PhysicsWorld::PhysicsWorld(WorldGrid& grid, float gravity) : grid(grid), gravity(gravity) {
	b2SetLengthUnitsPerMeter(10.0f);  // before b2DefaultWorldDef, which reads it
	b2WorldDef def = b2DefaultWorldDef();
	def.gravity = {0.0f, gravity};
	// Nothing moves further in a tick than the margin terrain shapes are kept around bodies,
	// so a body can't step into a chunk whose shapes were never built (960 cells/s).
	def.maximumLinearSpeed = BODY_SHAPE_MARGIN / PHYSICS_DT;
	world = b2CreateWorld(&def);
}

PhysicsWorld::~PhysicsWorld() { b2DestroyWorld(world); }

int PhysicsWorld::add_body(const BodyDef& def) {
	if (def.w <= 0 || def.h <= 0 || def.pixels.size() != (size_t)def.w * def.h) return -1;
	const MaterialTable& mt = grid.materials();
	Mask mask((size_t)def.w * def.h, 0);
	float total_density = 0;
	int minx = def.w, miny = def.h, maxx = -1, maxy = -1;
	for (int y = 0; y < def.h; ++y)
		for (int x = 0; x < def.w; ++x) {
			const MatId m = def.pixels[(size_t)y * def.w + x];
			if (m == NO_MAT) continue;
			mask[(size_t)y * def.w + x] = 1;
			total_density += mt.get(m).density;
			minx = std::min(minx, x), maxx = std::max(maxx, x);
			miny = std::min(miny, y), maxy = std::max(maxy, y);
		}
	if (maxx < 0) return -1;

	const int handle = (int)bodies.size();
	b2BodyDef bd = b2DefaultBodyDef();
	bd.type = b2_dynamicBody;
	bd.position = {def.x, def.y};
	bd.rotation = b2MakeRot(def.angle);
	bd.userData = handle_data(handle);
	bd.allowFastRotation = def.shape == BodyShape::Circle;
	const b2BodyId id = b2CreateBody(world, &bd);
	const b2Vec2 half = {def.w * 0.5f, def.h * 0.5f};

	switch (def.shape) {
		case BodyShape::Hull: {
			Loop hull = pixel_hull(mask, def.w, def.h);
			const b2ShapeDef sd = body_shape_def(def, total_density / polygon_area(hull));
			for (b2Vec2& p : hull) p = b2Sub(p, half);
			const b2Hull h = b2ComputeHull(hull.data(), (int)hull.size());
			const b2Polygon poly = b2MakePolygon(&h, 0.0f);
			b2CreatePolygonShape(id, &sd, &poly);
			break;
		}
		case BodyShape::Circle: {
			b2Circle circle;
			circle.radius = std::max(maxx + 1 - minx, maxy + 1 - miny) * 0.5f;
			circle.center = b2Sub({(minx + maxx + 1) * 0.5f, (miny + maxy + 1) * 0.5f}, half);
			const b2ShapeDef sd = body_shape_def(def, total_density / (B2_PI * circle.radius * circle.radius));
			b2CreateCircleShape(id, &sd, &circle);
			break;
		}
		case BodyShape::Boxes:
			for (const PixelRect& r : pixel_boxes(mask, def.w, def.h)) {
				float density = 0;
				for (int y = r.y; y < r.y + r.h; ++y)
					for (int x = r.x; x < r.x + r.w; ++x) density += mt.get(def.pixels[(size_t)y * def.w + x]).density;
				const b2ShapeDef sd = body_shape_def(def, density / (float)(r.w * r.h));
				const b2Polygon box = b2MakeOffsetBox(r.w * 0.5f, r.h * 0.5f,
				                                      b2Sub({r.x + r.w * 0.5f, r.y + r.h * 0.5f}, half), b2Rot_identity);
				b2CreatePolygonShape(id, &sd, &box);
			}
			break;
	}

	auto body = std::make_unique<Body>();
	body->id = id;
	body->w = def.w;
	body->h = def.h;
	body->pixels = def.pixels;
	body->opaque = (int)std::count(mask.begin(), mask.end(), 1);
	bodies.push_back(std::move(body));
	++stats_.bodies;
	return handle;
}

void PhysicsWorld::remove_body(int handle) {
	if (!has_body(handle)) return;
	wake_footprint(*bodies[(size_t)handle]);
	const bool creature = bodies[(size_t)handle]->creature;
	if (creature) b2DestroyBody(bodies[(size_t)handle]->foot);
	b2DestroyBody(bodies[(size_t)handle]->id);
	bodies[(size_t)handle].reset();
	--(creature ? stats_.creatures : stats_.bodies);
}

int PhysicsWorld::add_creature(const CreatureDef& def) {
	if (def.w <= 0 || def.h <= 0) return -1;
	const int handle = (int)bodies.size();
	b2BodyDef bd = b2DefaultBodyDef();
	bd.type = b2_dynamicBody;
	bd.position = {def.x, def.y};
	bd.userData = handle_data(handle);
	bd.motionLocks.angularZ = true;  // creatures stay upright
	bd.enableSleep = false;          // always driven by game code
	bd.gravityScale = def.gravity_scale;
	const b2BodyId id = b2CreateBody(world, &bd);
	b2ShapeDef sd = b2DefaultShapeDef();
	sd.density = def.mass / (float)(def.w * def.h);
	sd.material.friction = def.friction;
	sd.filter.categoryBits = CAT_CREATURE;
	sd.filter.maskBits = CAT_BODY | CAT_CREATURE;  // terrain is the grid pass's job
	sd.filter.groupIndex = handle + 1;              // ...except its own foot (below)
	sd.enableHitEvents = true;
	const b2Polygon box = b2MakeBox(def.w * 0.5f, def.h * 0.5f);
	b2CreatePolygonShape(id, &sd, &box);

	// The foot: a kinematic segment as wide as the creature, parked under it at the grid floor
	// by place_foot. Same positive group as its creature (always collide), mask 0 (nothing else).
	b2BodyDef fd = b2DefaultBodyDef();
	fd.type = b2_kinematicBody;
	fd.position = {def.x, def.y + def.h * 0.5f};
	fd.isEnabled = false;
	const b2BodyId foot = b2CreateBody(world, &fd);
	b2ShapeDef fs = b2DefaultShapeDef();
	fs.material.friction = def.friction;
	fs.filter.categoryBits = CAT_FOOT;
	fs.filter.maskBits = 0;
	fs.filter.groupIndex = handle + 1;
	const b2Segment segment = {{-def.w * 0.5f, 0.0f}, {def.w * 0.5f, 0.0f}};
	b2CreateSegmentShape(foot, &fs, &segment);

	auto body = std::make_unique<Body>();
	body->id = id;
	body->w = def.w;
	body->h = def.h;
	body->creature = true;
	body->step_height = def.step_height;
	body->foot = foot;
	bodies.push_back(std::move(body));
	++stats_.creatures;
	return handle;
}

bool PhysicsWorld::is_creature(int handle) const { return has_body(handle) && bodies[(size_t)handle]->creature; }

const CreatureState& PhysicsWorld::creature_state(int handle) const {
	static const CreatureState none;
	return is_creature(handle) ? bodies[(size_t)handle]->state : none;
}

void PhysicsWorld::teleport(int handle, float x, float y, float angle) {
	if (!has_body(handle)) return;
	Body& body = *bodies[(size_t)handle];
	wake_footprint(body);
	b2Body_SetTransform(body.id, {x, y}, b2MakeRot(angle));
	b2Body_SetAwake(body.id, true);
}

// A sleeping body stamps without waking the cells around it, so sand resting on it sleeps
// too; when the body goes, the sim must look at that place again.
void PhysicsWorld::wake_footprint(const Body& body) {
	const b2AABB box = b2Body_ComputeBounds(body.id);
	for (int y = (int)std::floor(box.lowerBound.y) - 1; y <= (int)std::ceil(box.upperBound.y); ++y)
		for (int x = (int)std::floor(box.lowerBound.x) - 1; x <= (int)std::ceil(box.upperBound.x); ++x) grid.wake(x, y);
}

bool PhysicsWorld::has_body(int handle) const {
	return handle >= 0 && (size_t)handle < bodies.size() && bodies[(size_t)handle] != nullptr;
}

b2BodyId PhysicsWorld::body_id(int handle) const {
	return has_body(handle) ? bodies[(size_t)handle]->id : b2_nullBodyId;
}

// Its own chunk is loaded and active, and every chunk its AABB + margin touches is loaded,
// so it never steps next to ground that has no shapes.
bool PhysicsWorld::wants_enabled(const Body& body) const {
	const b2Vec2 p = b2Body_GetPosition(body.id);
	const ChunkCoord own = chunk_of((int)std::floor(p.x), (int)std::floor(p.y));
	if (!grid.find(own)) return false;
	if (active_rect && !active_rect->contains(own)) return false;
	const ChunkRect around = chunks_around(b2Body_ComputeBounds(body.id), BODY_SHAPE_MARGIN);
	for (int y = around.y0; y <= around.y1; ++y)
		for (int x = around.x0; x <= around.x1; ++x)
			if (!grid.find({x, y})) return false;
	return true;
}

void PhysicsWorld::update_terrain() {
	const Clock::time_point start = Clock::now();
	stats_.shape_builds = 0;
	++tick_;

	// Shapes of chunks that are gone go with them.
	for (auto it = terrain.begin(); it != terrain.end();) {
		const ChunkCoord coord{(int)(it->first >> 32), (int)(uint32_t)it->first};
		if (grid.find(coord)) {
			++it;
			continue;
		}
		b2DestroyBody(it->second.id);
		it = terrain.erase(it);
	}

	Mask mask;
	for (const std::unique_ptr<Body>& body : bodies) {
		if (!body || body->creature || !wants_enabled(*body)) continue;
		const ChunkRect around = chunks_around(b2Body_ComputeBounds(body->id), BODY_SHAPE_MARGIN);
		for (int y = around.y0; y <= around.y1; ++y)
			for (int x = around.x0; x <= around.x1; ++x) {
				Chunk* chunk = grid.find({x, y});
				if (!chunk) continue;
				const auto it = terrain.find(chunk_key({x, y}));
				if (it != terrain.end() && !chunk->shape_dirty.load()) continue;
				// Too soon: leave the flag set so the chunk is looked at again next tick.
				if (it != terrain.end() && tick_ - it->second.built_tick < SHAPE_REBUILD_INTERVAL) continue;
				chunk->shape_dirty.store(false);
				chunk_solid_mask(grid, *chunk, mask);
				if (it != terrain.end() && it->second.mask == mask) continue;
				build_terrain({x, y}, std::move(mask));
				mask = Mask();
			}
	}
	stats_.terrain_bodies = (int)terrain.size();
	stats_.shape_ms = ms_since(start);
}

void PhysicsWorld::build_terrain(ChunkCoord coord, Mask&& mask) {
	const auto it = terrain.find(chunk_key(coord));
	if (it != terrain.end()) b2DestroyBody(it->second.id);

	b2BodyDef bd = b2DefaultBodyDef();
	bd.position = {(float)(coord.x * CHUNK), (float)(coord.y * CHUNK)};
	const b2BodyId id = b2CreateBody(world, &bd);
	// Grippier than Box2D's 0.6: sand settles at 45 degrees, and a wheel with rubber-like
	// friction (1.5) needs about 1.0 combined to climb that.
	b2SurfaceMaterial ground = b2DefaultSurfaceMaterial();
	ground.friction = TERRAIN_FRICTION;
	b2ChainDef cd = b2DefaultChainDef();
	cd.materials = &ground;
	cd.materialCount = 1;
	cd.isLoop = true;
	cd.filter.categoryBits = CAT_TERRAIN;
	cd.filter.maskBits = CAT_BODY;  // creatures meet terrain through the grid pass only
	for (const Loop& loop : trace_loops(mask, CHUNK, CHUNK)) {
		const Loop simple = simplify_loop(loop, 1.0f);
		cd.points = simple.data();
		cd.pointCount = (int)simple.size();
		b2CreateChain(id, &cd);
	}
	terrain[chunk_key(coord)] = Terrain{id, std::move(mask), tick_};
	++stats_.shape_builds;
	wake_bodies_in(coord);
}

// The old shapes' contacts are gone, so a body resting on them must re-check its support.
void PhysicsWorld::wake_bodies_in(ChunkCoord coord) {
	for (const std::unique_ptr<Body>& body : bodies) {
		if (!body || !b2Body_IsEnabled(body->id)) continue;
		const ChunkRect around = chunks_around(b2Body_ComputeBounds(body->id), 1);
		if (around.contains(coord)) b2Body_SetAwake(body->id, true);
	}
}

void PhysicsWorld::step() {
	const Clock::time_point start = Clock::now();
	// Jointed bodies go on and off together: Box2D drops a joint whose body is disabled, and a
	// car must not lose a wheel at the edge of the active zone.
	std::vector<char> wants(bodies.size(), 0);
	for (size_t h = 0; h < bodies.size(); ++h) wants[h] = bodies[h] && wants_enabled(*bodies[h]);
	std::vector<int> stack;
	b2JointId joints[16];
	for (size_t h = 0; h < bodies.size(); ++h) {
		if (!bodies[h] || wants[h]) continue;
		stack.push_back((int)h);
		while (!stack.empty()) {
			const b2BodyId id = bodies[(size_t)stack.back()]->id;
			stack.pop_back();
			const int n = b2Body_GetJoints(id, joints, 16);
			for (int j = 0; j < n; ++j)
				for (b2BodyId other : {b2Joint_GetBodyA(joints[j]), b2Joint_GetBodyB(joints[j])}) {
					const int o = handle_of(other);
					if (o >= 0 && wants[(size_t)o]) {
						wants[(size_t)o] = 0;
						stack.push_back(o);
					}
				}
		}
	}
	for (size_t h = 0; h < bodies.size(); ++h) {
		Body* body = bodies[h].get();
		if (!body) continue;
		const bool want = wants[h] != 0;
		if (want != b2Body_IsEnabled(body->id)) {
			if (want) b2Body_Enable(body->id);
			else b2Body_Disable(body->id);
			if (body->creature && !want && body->foot_on) {
				b2Body_Disable(body->foot);
				body->foot_on = false;
			}
		}
		if (want) {
			body->left = false;
			continue;
		}
		const b2Vec2 p = b2Body_GetPosition(body->id);
		if (!body->left && !grid.find(chunk_of((int)std::floor(p.x), (int)std::floor(p.y)))) {
			body->left = true;
			left_world.push_back((int)h);
		}
	}

	for (const std::unique_ptr<Body>& body : bodies) {
		if (!body || !b2Body_IsEnabled(body->id)) continue;
		if (body->submerged > 0) apply_buoyancy(*body);
		if (body->creature) {
			const b2Vec2 p = b2Body_GetPosition(body->id);
			body->prev_x = p.x;
			body->prev_y = p.y;
		}
	}

	b2World_Step(world, PHYSICS_DT, PHYSICS_SUBSTEPS);

	hits_.clear();
	const b2ContactEvents events = b2World_GetContactEvents(world);
	for (int i = 0; i < events.hitCount; ++i) {
		const b2ContactHitEvent& e = events.hitEvents[i];
		int a = handle_of(b2Shape_GetBody(e.shapeIdA)), b = handle_of(b2Shape_GetBody(e.shapeIdB));
		float nx = e.normal.x, ny = e.normal.y;
		if (a < 0) {  // keep the body first; flip the normal with it
			std::swap(a, b);
			nx = -nx;
			ny = -ny;
		}
		if (a < 0) continue;
		hits_.push_back({a, b, e.point.x, e.point.y, nx, ny, e.approachSpeed});
	}

	int awake = 0;
	for (const std::unique_ptr<Body>& body : bodies)
		if (body && !body->creature && b2Body_IsEnabled(body->id) && b2Body_IsAwake(body->id)) ++awake;
	stats_.awake = awake;

	const Clock::time_point creatures_start = Clock::now();
	for (const std::unique_ptr<Body>& body : bodies)
		if (body && body->creature && b2Body_IsEnabled(body->id)) creature_pass(*body);
	stats_.creature_ms = ms_since(creatures_start);
	stats_.physics_ms = ms_since(start) - stats_.creature_ms;
}

// Box2D moved the creature against bodies and other creatures; now the cells get their say:
// sweep from where it was to where Box2D put it, and keep what the sweep allows.
void PhysicsWorld::creature_pass(Body& body) {
	const b2Vec2 p = b2Body_GetPosition(body.id);
	b2Vec2 v = b2Body_GetLinearVelocity(body.id);
	const GridPass r = creature_grid_pass(grid, particles, body.prev_x, body.prev_y, p.x, p.y, body.w, body.h,
	                                      body.step_height, body.state.floor, b2Length(v));
	if (r.x != p.x || r.y != p.y) b2Body_SetTransform(body.id, {r.x, r.y}, b2Rot_identity);
	if (r.stopped_x) v.x = 0;
	if ((r.floor && v.y > 0) || (r.ceiling && v.y < 0)) v.y = 0;
	b2Body_SetLinearVelocity(body.id, v);

	// Standing on a body or another creature counts as the floor too: a touching contact whose
	// normal points down from the creature.
	bool on_body = false;
	b2ContactData contacts[8];
	const int n = b2Body_GetContactData(body.id, contacts, 8);
	for (int i = 0; i < n && !on_body; ++i) {
		if (contacts[i].manifold.pointCount == 0) continue;
		const bool is_a = B2_ID_EQUALS(b2Shape_GetBody(contacts[i].shapeIdA), body.id);
		const float down = is_a ? contacts[i].manifold.normal.y : -contacts[i].manifold.normal.y;
		on_body = down > 0.7f;
	}
	body.state.floor = r.floor || on_body;
	body.state.ceiling = r.ceiling;
	body.state.wall = r.wall;
	body.state.submerged_fraction = (float)r.submerged / (float)(body.w * body.h);
	body.state.liquid = r.liquid;
	place_foot(body);
}

// The highest blocking cell within FOOT_REACH under any of the box's columns; the foot goes on
// its top edge, or away when there is none (airborne). It sits at the real floor, not at the
// creature's feet, so walking down a slope or off a ledge still falls to the ground.
void PhysicsWorld::place_foot(Body& body) {
	constexpr int FOOT_REACH = 4;
	const b2Vec2 p = b2Body_GetPosition(body.id);
	const int x0 = (int)std::floor(p.x - body.w * 0.5f + 1e-4f), x1 = (int)std::floor(p.x + body.w * 0.5f - 1e-4f);
	const int below = (int)std::floor(p.y + body.h * 0.5f - 1e-4f) + 1;
	int floor_row = INT32_MAX;
	for (int x = x0; x <= x1; ++x)
		for (int y = below; y < below + FOOT_REACH && y < floor_row; ++y)
			if (creature_blocks(grid, x, y)) floor_row = y;
	const bool on = floor_row != INT32_MAX;
	if (on) b2Body_SetTransform(body.foot, {p.x, (float)floor_row}, b2Rot_identity);
	if (on != body.foot_on) {
		if (on) b2Body_Enable(body.foot);
		else b2Body_Disable(body.foot);
		body.foot_on = on;
	}
}

// Archimedes on the liquid the last stamp covered, at its centroid (so a tipped crate rights
// itself), plus drag in proportion to how much of the body is under.
void PhysicsWorld::apply_buoyancy(Body& body) {
	constexpr float LIQUID_DRAG = 2.0f;  // per second
	const float fraction = (float)body.submerged / (float)body.opaque;
	const b2Vec2 centroid = {body.sum_x / body.submerged, body.sum_y / body.submerged};
	b2Body_ApplyForce(body.id, {0.0f, -body.liquid_density * gravity}, centroid, true);
	const float mass = b2Body_GetMass(body.id);
	b2Body_ApplyForceToCenter(body.id, b2MulSV(-LIQUID_DRAG * fraction * mass, b2Body_GetLinearVelocity(body.id)),
	                          true);
	b2Body_ApplyTorque(body.id,
	                   -LIQUID_DRAG * fraction * b2Body_GetRotationalInertia(body.id) * b2Body_GetAngularVelocity(body.id),
	                   true);
}

void PhysicsWorld::stamp() {
	const Clock::time_point start = Clock::now();
	const MaterialTable& mt = grid.materials();
	for (const std::unique_ptr<Body>& owned : bodies) {
		if (!owned || owned->creature || !b2Body_IsEnabled(owned->id)) continue;  // creatures aren't pixels
		Body& body = *owned;
		body.submerged = 0;
		body.liquid_density = body.sum_x = body.sum_y = 0;
		body.stamped_awake = b2Body_IsAwake(body.id);
		// Inverse mapping: every cell in the AABB looks up the pixel under its centre, so a
		// rotated body has no holes.
		const b2AABB box = b2Body_ComputeBounds(body.id);
		const int x0 = (int)std::floor(box.lowerBound.x), x1 = (int)std::ceil(box.upperBound.x);
		const int y0 = (int)std::floor(box.lowerBound.y), y1 = (int)std::ceil(box.upperBound.y);
		for (int wy = y0; wy < y1; ++wy)
			for (int wx = x0; wx < x1; ++wx) {
				const b2Vec2 local = b2Body_GetLocalPoint(body.id, {wx + 0.5f, wy + 0.5f});
				const int px = (int)std::floor(local.x + body.w * 0.5f), py = (int)std::floor(local.y + body.h * 0.5f);
				if (px < 0 || py < 0 || px >= body.w || py >= body.h) continue;
				const MatId mat = body.pixels[(size_t)py * body.w + px];
				if (mat == NO_MAT) continue;
				Chunk* chunk = grid.find(chunk_of(wx, wy));
				if (!chunk) continue;
				Cell& cell = chunk->at(floor_mod(wx, CHUNK), floor_mod(wy, CHUNK));
				if (cell.flags & FLAG_BODY) continue;  // another body got here first
				const CompiledMaterial& under = mt.get(cell.mat);
				if (under.cls == MatClass::Solid) continue;  // terrain stays; Box2D separates them
				if (under.cls == MatClass::Liquid) {
					++body.submerged;
					body.liquid_density += under.density;
					body.sum_x += wx + 0.5f;
					body.sum_y += wy + 0.5f;
				}
				body.held.push_back({wx, wy, cell});
				cell = spawn_cell(mat, mt, grid.seed(), wx, wy);
				cell.flags |= FLAG_BODY;
				if (body.stamped_awake) grid.wake(wx, wy);
			}
	}
	stats_.stamp_ms = ms_since(start);
}

void PhysicsWorld::unstamp() {
	const Clock::time_point start = Clock::now();
	for (const std::unique_ptr<Body>& body : bodies) {
		if (!body) continue;
		for (const Body::Held& h : body->held) {
			Chunk* chunk = grid.find(chunk_of(h.x, h.y));
			if (!chunk) continue;
			Cell& cell = chunk->at(floor_mod(h.x, CHUNK), floor_mod(h.y, CHUNK));
			if (!(cell.flags & FLAG_BODY)) continue;
			cell = h.cell;
			if (body->stamped_awake) grid.wake(h.x, h.y);
		}
		body->held.clear();
	}
	stats_.stamp_ms += ms_since(start);
}

namespace {

// Frames at the anchor, rotated so that their x axis points along `world_angle`.
void set_frames(b2JointDef& base, b2BodyId a, b2BodyId b, b2Vec2 anchor, float world_angle) {
	base.bodyIdA = a;
	base.bodyIdB = b;
	base.localFrameA.p = b2Body_GetLocalPoint(a, anchor);
	base.localFrameA.q = b2MakeRot(world_angle - b2Rot_GetAngle(b2Body_GetRotation(a)));
	base.localFrameB.p = b2Body_GetLocalPoint(b, anchor);
	base.localFrameB.q = b2MakeRot(world_angle - b2Rot_GetAngle(b2Body_GetRotation(b)));
}

}  // namespace

b2JointId PhysicsWorld::add_revolute(int a, int b, float x, float y, const RevoluteParams& params) {
	if (!has_body(a) || !has_body(b) || a == b) return b2_nullJointId;
	b2RevoluteJointDef jd = b2DefaultRevoluteJointDef();
	set_frames(jd.base, body_id(a), body_id(b), {x, y}, 0.0f);
	jd.enableLimit = params.limit;
	jd.lowerAngle = params.lower;
	jd.upperAngle = params.upper;
	jd.enableMotor = params.motor;
	jd.motorSpeed = params.speed;
	jd.maxMotorTorque = params.max_torque;
	return b2CreateRevoluteJoint(world, &jd);
}

b2JointId PhysicsWorld::add_wheel(int a, int b, float x, float y, const WheelParams& params) {
	if (!has_body(a) || !has_body(b) || a == b) return b2_nullJointId;
	b2WheelJointDef jd = b2DefaultWheelJointDef();
	set_frames(jd.base, body_id(a), body_id(b), {x, y}, std::atan2(params.axis_y, params.axis_x));
	jd.enableSpring = true;
	jd.hertz = params.hertz;
	jd.dampingRatio = params.damping;
	jd.enableMotor = params.motor;
	jd.motorSpeed = params.speed;
	jd.maxMotorTorque = params.max_torque;
	return b2CreateWheelJoint(world, &jd);
}

void PhysicsWorld::remove_joint(b2JointId joint) {
	if (b2Joint_IsValid(joint)) b2DestroyJoint(joint);
}

std::vector<int> PhysicsWorld::take_left_world() {
	std::vector<int> out;
	out.swap(left_world);
	return out;
}

bool PhysicsWorld::raycast(float x0, float y0, float x1, float y1, int& handle, float& hx, float& hy,
                           float& nx, float& ny) const {
	b2QueryFilter filter = b2DefaultQueryFilter();
	filter.maskBits = CAT_BODY | CAT_CREATURE;
	const b2RayResult r = b2World_CastRayClosest(world, {x0, y0}, {x1 - x0, y1 - y0}, filter);
	if (!r.hit) return false;
	handle = handle_of(b2Shape_GetBody(r.shapeId));
	hx = r.point.x;
	hy = r.point.y;
	nx = r.normal.x;
	ny = r.normal.y;
	return handle >= 0;
}
