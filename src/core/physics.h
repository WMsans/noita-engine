#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "box2d/box2d.h"
#include "cell.h"
#include "coords.h"
#include "outline.h"

class WorldGrid;
class ParticlePool;

enum class BodyShape { Hull, Circle, Boxes };

// A body's pixels and spawn pose. Box2D works in cells (see PhysicsWorld), y down.
struct BodyDef {
	int w = 0, h = 0;
	std::vector<MatId> pixels;  // w*h row-major, NO_MAT = transparent
	BodyShape shape = BodyShape::Hull;
	float friction = 0.6f, restitution = 0.1f;
	float x = 0, y = 0, angle = 0;  // the image centre's world position, radians
};

// An upright creature: a w x h box that collides with bodies and creatures in Box2D and with
// the cells through the grid pass (see creature.h).
struct CreatureDef {
	int w = 8, h = 16, step_height = 3;
	float mass = 128, friction = 0.6f, gravity_scale = 1;
	float x = 0, y = 0;  // the box centre
};

// What the grid pass and the contacts found last tick.
struct CreatureState {
	bool floor = false, ceiling = false;
	int wall = 0;  // -1 left, +1 right
	float submerged_fraction = 0;
	MatId liquid = MAT_AIR;
};

struct BodyHit {
	int a, b;  // body handles; b = -1 for terrain
	float x, y, nx, ny, speed;  // point, normal from a to b, approach speed (cells/s)
};

struct PhysicsStats {
	int bodies = 0, awake = 0, terrain_bodies = 0, shape_builds = 0, creatures = 0;
	double physics_ms = 0, shape_ms = 0, stamp_ms = 0, creature_ms = 0;
};

// Joint settings; angles in radians, speeds in rad/s, torques in Box2D units (cells).
struct RevoluteParams {
	bool limit = false;
	float lower = 0, upper = 0;
	bool motor = false;
	float speed = 0, max_torque = 0;
};
struct WheelParams {
	float axis_x = 0, axis_y = 1;  // world-space suspension axis at creation
	float hertz = 4, damping = 0.7f;
	bool motor = false;
	float speed = 0, max_torque = 0;
};

constexpr uint64_t CAT_TERRAIN = 1;
constexpr uint64_t CAT_BODY = 2;
constexpr uint64_t CAT_CREATURE = 4;
constexpr uint64_t CAT_FOOT = 8;  // a creature's floor proxy; meets only its own creature (group)
constexpr float TERRAIN_FRICTION = 0.8f;
// A chunk's shapes rebuild at most this often (ticks): terrain collision may lag the pixels by
// up to 50 ms, and a chunk under a sand faucet costs a quarter of the builds.
constexpr uint32_t SHAPE_REBUILD_INTERVAL = 4;
constexpr int BODY_SHAPE_MARGIN = 16;  // cells around a body whose chunks keep terrain shapes
constexpr float PHYSICS_DT = 1.0f / 60.0f;
constexpr int PHYSICS_SUBSTEPS = 4;

// One Box2D world in cell units (b2SetLengthUnitsPerMeter(10)), one static terrain body per
// chunk near bodies, and the bodies themselves.
// ponytail: synchronous shape builds; move to a worker if shape_ms > 1 ms
class PhysicsWorld {
public:
	PhysicsWorld(WorldGrid& grid, float gravity);  // gravity in cells/s^2, +y
	~PhysicsWorld();
	PhysicsWorld(const PhysicsWorld&) = delete;
	PhysicsWorld& operator=(const PhysicsWorld&) = delete;

	int add_body(const BodyDef& def);  // handle >= 0, or -1 if it has no opaque pixels
	// Same handle space as bodies; creatures are never stamped and need no terrain shapes.
	int add_creature(const CreatureDef& def);
	bool is_creature(int handle) const;
	const CreatureState& creature_state(int handle) const;
	void set_particle_pool(ParticlePool* pool) { particles = pool; }  // knock-out and splashes
	void remove_body(int handle);      // also destroys its joints
	// Moves a body, waking what was resting on it at its old place.
	void teleport(int handle, float x, float y, float angle);
	bool has_body(int handle) const;
	b2BodyId body_id(int handle) const;

	// Bodies outside it are disabled; nullopt (the default) makes every loaded chunk active.
	void set_active_rect(std::optional<ChunkRect> rect) { active_rect = rect; }
	// Shapes for chunks near bodies that want to be enabled; drops shapes of unloaded chunks.
	void update_terrain();
	void step();  // zones, buoyancy from the last stamp, b2World_Step, hits
	// Writes every enabled body's pixels into the grid with FLAG_BODY, holding the air, gas,
	// powder or liquid they cover; non-body solids and other bodies' pixels are skipped.
	void stamp();    // before Simulator::tick
	void unstamp();  // after it: every cell still carrying FLAG_BODY gets its held cell back

	// The anchor is in world cells; both joint frames start aligned with the world, so a
	// revolute's angle and a wheel's axis are measured from the pose at creation.
	b2JointId add_revolute(int a, int b, float x, float y, const RevoluteParams& params);
	b2JointId add_wheel(int a, int b, float x, float y, const WheelParams& params);
	void remove_joint(b2JointId joint);  // no-op if it went with one of its bodies

	const std::vector<BodyHit>& hits() const { return hits_; }
	std::vector<int> take_left_world();  // handles whose chunk unloaded since the last call
	// Closest enabled body or creature along the segment.
	bool raycast(float x0, float y0, float x1, float y1, int& handle, float& hx, float& hy, float& nx,
	             float& ny) const;
	const PhysicsStats& stats() const { return stats_; }

private:
	struct Body {
		b2BodyId id;
		int w, h;
		std::vector<MatId> pixels;
		int opaque = 0;
		bool left = false;  // reported by take_left_world and not enabled since
		// Creatures only.
		bool creature = false;
		int step_height = 0;
		float prev_x = 0, prev_y = 0;  // before this tick's Box2D step
		CreatureState state;
		// The grid floor under it, told to Box2D, so a crate on its head or a creature on its
		// shoulders rests on it instead of pressing it into the ground.
		b2BodyId foot = b2_nullBodyId;
		bool foot_on = false;
		struct Held {
			int x, y;
			Cell cell;
		};
		std::vector<Held> held;  // cells under the stamped pixels
		bool stamped_awake = false;
		// From the last stamp: liquid pixels under the body, their summed density and position.
		int submerged = 0;
		float liquid_density = 0, sum_x = 0, sum_y = 0;
	};
	struct Terrain {
		b2BodyId id;
		Mask mask;
		uint32_t built_tick;
	};

	bool wants_enabled(const Body& body) const;
	void wake_footprint(const Body& body);
	void build_terrain(ChunkCoord coord, Mask&& mask);
	void wake_bodies_in(ChunkCoord coord);

	void apply_buoyancy(Body& body);
	void creature_pass(Body& body);
	void place_foot(Body& body);

	WorldGrid& grid;
	float gravity;
	b2WorldId world;
	std::vector<std::unique_ptr<Body>> bodies;  // index = handle; null once removed
	std::unordered_map<int64_t, Terrain> terrain;
	std::optional<ChunkRect> active_rect;
	std::vector<BodyHit> hits_;
	std::vector<int> left_world;
	ParticlePool* particles = nullptr;
	uint32_t tick_ = 0;  // counts update_terrain calls
	PhysicsStats stats_{};
};
