extends SceneTree

# Headless Godot test runner: every `test_*` method taking no arguments is a test and
# returns true on success. Run with:
#   godot --headless --path . -s tests/godot/run_tests.gd


func _initialize() -> void:
	var failures: PackedStringArray = []
	for method: Dictionary in get_method_list():
		var method_name: String = method["name"]
		if not method_name.begins_with("test_") or not method["args"].is_empty():
			continue
		# `await` on a plain value returns it immediately, so async and sync tests share one call.
		if not await call(method_name):
			failures.append(method_name)

	if failures.is_empty():
		print("ALL GODOT TESTS PASSED")
		quit(0)
	else:
		for failed: String in failures:
			printerr("FAILED: " + failed)
		quit(1)


func test_extension_loaded() -> bool:
	return ClassDB.class_exists("PixelWorld")


func test_default_set_validates() -> bool:
	var mat_set: PixelMaterialSet = load("res://materials/default_set.tres")
	if mat_set == null:
		printerr("  default_set.tres did not load")
		return false
	if mat_set.materials.size() != 12 or mat_set.reactions.size() != 5:
		printerr("  starter set has %d materials, %d reactions" % [mat_set.materials.size(), mat_set.reactions.size()])
		return false
	var error: String = mat_set.validate()
	if error != "":
		printerr("  validate() failed: " + error)
	return error == ""


# Material ids are 1..n in `materials` order (core MaterialTable::compile), so an index
# pins the id: rock is 1, water 4, fire 8.
func test_starter_set_ids_and_fields() -> bool:
	var mat_set: PixelMaterialSet = load("res://materials/default_set.tres")
	var expected: Array[StringName] = [&"rock", &"wood", &"sand", &"water", &"oil", &"lava", &"acid", &"fire", &"steam", &"smoke", &"dirt", &"grass"]
	for i: int in expected.size():
		if mat_set.materials[i].name != expected[i]:
			printerr("  material %d is '%s', expected '%s'" % [i + 1, mat_set.materials[i].name, expected[i]])
			return false
	var water: PixelMaterial = mat_set.materials[3]
	var fire: PixelMaterial = mat_set.materials[7]
	var steam: PixelMaterial = mat_set.materials[8]
	var water_lava: PixelReaction = mat_set.reactions[0]
	var checks: Array[bool] = [
		water.high_temperature == 100 and water.high_into == &"steam",
		fire.decays_into == &"smoke" and fire.lifetime == 24,
		steam.conductivity == 0.0 and steam.low_into == &"water" and steam.decays_into == &"water",
		is_equal_approx(water.light_opacity, 0.3) and is_equal_approx(fire.light_opacity, 0.05),
		water_lava.a == &"water" and water_lava.a_into == &"steam" and water_lava.b_into == &"rock",
	]
	for ok: bool in checks:
		if not ok:
			printerr("  starter set field check failed: ", checks)
			return false
	return true


func _make_world() -> PixelWorld:
	var world: PixelWorld = PixelWorld.new()
	world.material_set = load("res://materials/default_set.tres")
	world.worker_threads = 1  # single-threaded: deterministic, and no threads in the test run
	root.add_child(world)
	# This runner starts inside SceneTree._initialize(), before the root enters the tree, so a
	# node added here only gets _ready (and with it its first chunk load) on the first frame.
	await process_frame
	return world


func test_sand_falls() -> bool:
	var world: PixelWorld = await _make_world()
	world.set_cell(Vector2i(0, -200), &"sand")
	world.step(30)
	var top: StringName = world.get_material_at(Vector2i(0, -200))
	var found := false
	for y: int in range(-199, 256):
		if world.get_material_at(Vector2i(0, y)) == &"sand":
			found = true
			break
	world.free()
	if top != &"air" or not found:
		printerr("  (0,-200) is '%s' after 30 ticks, sand in column below: %s" % [top, found])
		return false
	return true


func test_negative_coords() -> bool:
	var world: PixelWorld = await _make_world()
	var wrote: bool = world.set_cell(Vector2i(-300, -100), &"water")
	var mat: StringName = world.get_material_at(Vector2i(-300, -100))
	world.free()
	if not wrote or mat != &"water":
		printerr("  set_cell(-300,-100) returned %s, get_material_at returned '%s'" % [wrote, mat])
		return false
	return true


func test_unloaded_write_is_noop() -> bool:
	var world: PixelWorld = await _make_world()
	var wrote: bool = world.set_cell(Vector2i(100000, 0), &"sand")
	var filled: int = world.fill_circle(Vector2i(100000, 0), 5, &"sand")
	world.free()
	if wrote or filled != 0:
		printerr("  unloaded write returned %s and filled %d cells, expected false / 0" % [wrote, filled])
		return false
	return true


func test_unknown_material() -> bool:
	var world: PixelWorld = await _make_world()
	# The error this pushes is the expected behaviour, not a test failure.
	var wrote: bool = world.set_cell(Vector2i(0, 0), &"unobtainium")
	var filled: int = world.fill_circle(Vector2i(0, 0), 2, &"unobtainium")
	world.free()
	if wrote or filled != 0:
		printerr("  unknown material returned %s / %d, expected false / 0" % [wrote, filled])
		return false
	return true


func test_chunks_follow_target() -> bool:
	var world: PixelWorld = await _make_world()
	var target: Node2D = Node2D.new()
	target.position = Vector2(5000, 0)
	root.add_child(target)
	world.follow_target = target.get_path()
	await process_frame
	await process_frame
	var far: bool = world.is_chunk_loaded(Vector2i(78, 0))
	var origin: bool = world.is_chunk_loaded(Vector2i(0, 0))
	world.free()
	target.free()
	if not far or origin:
		printerr("  chunk(78,0) loaded=%s, chunk(0,0) loaded=%s" % [far, origin])
		return false
	return true


func test_stats_keys() -> bool:
	var world: PixelWorld = await _make_world()
	var stats: Dictionary = world.get_stats()
	world.free()
	var keys: Array[String] = ["sim_ms", "upload_ms", "chunks_loaded", "chunks_simulated", "cells_updated", "tick", "chunks_active", "gen_pending", "gen_ms", "chunks_generated", "stream_ms", "view_missing", "particles_material", "particles_visual", "particles_dropped", "particle_ms"]
	for key: String in keys:
		if not stats.has(key):
			printerr("  get_stats() is missing '%s'; keys are %s" % [key, stats.keys()])
			return false
	# The arena path has no streamer: everything is active and the streaming stats are 0.
	for key: String in ["gen_pending", "gen_ms", "chunks_generated", "stream_ms", "view_missing"]:
		if stats[key] != 0:
			printerr("  arena path has %s = %s, expected 0" % [key, stats[key]])
			return false
	if stats["chunks_active"] != stats["chunks_loaded"]:
		printerr("  arena path has chunks_active %s, chunks_loaded %s" % [stats["chunks_active"], stats["chunks_loaded"]])
		return false
	return true


# --- particles, explode, raycast (Plan 3) ---------------------------------------------------

func _count_material(world: PixelWorld, rect: Rect2i, material: StringName) -> int:
	var n := 0
	for y: int in range(rect.position.y, rect.end.y):
		for x: int in range(rect.position.x, rect.end.x):
			if world.get_material_at(Vector2i(x, y)) == material:
				n += 1
	return n


func test_particles_land() -> bool:
	var world: PixelWorld = await _make_world()
	var strip := Rect2i(-60, -100, 110, 356)  # under the spawn points, down to the arena floor
	var before := _count_material(world, strip, &"sand")
	# Five per spot is what fits: a particle landing on its siblings searches at most 4 cells up.
	for i: int in 10:
		var pooled: int = world.spawn_particles(&"sand", Vector2(-50 + 10 * i, -100), Vector2.ZERO, 5, 0.0)
		if pooled != 5:
			printerr("  spawn_particles returned %d, expected 5" % pooled)
			world.free()
			return false
	world.step(300)
	var stats: Dictionary = world.get_stats()
	var after := _count_material(world, strip, &"sand")
	world.free()
	if stats.particles_material != 0 or stats.particles_dropped != 0 or after - before != 50:
		printerr("  in flight %d, dropped %d, sand gained %d (expected 0, 0, 50)" % [stats.particles_material, stats.particles_dropped, after - before])
		return false
	return true


func test_visual_particles_expire() -> bool:
	var world: PixelWorld = await _make_world()
	var pooled: int = world.spawn_visual_particles(Color.RED, Vector2.ZERO, Vector2(0, -60), 20, PI, 0.5)
	world.step(29)
	var alive: int = world.get_stats().particles_visual
	world.step(1)
	var after: int = world.get_stats().particles_visual
	world.free()
	if pooled != 20 or alive != 20 or after != 0:
		printerr("  pooled %d, alive after 29 ticks %d, after 30 %d (expected 20, 20, 0)" % [pooled, alive, after])
		return false
	return true


func test_explode_and_raycast() -> bool:
	var world: PixelWorld = await _make_world()
	world.fill_rect(Rect2i(-20, -20, 40, 40), &"sand")
	var removed: int = world.explode(Vector2i(0, 0), 8, 300.0)
	var centre: StringName = world.get_material_at(Vector2i(0, 0))
	var hit: Dictionary = world.raycast(Vector2(-30.5, 0.5), Vector2(0.5, 0.5), PixelWorld.RAY_POWDER)
	world.free()
	if removed <= 0 or centre != &"air":
		printerr("  explode removed %d, centre is '%s'" % [removed, centre])
		return false
	if hit.is_empty() or hit.position != Vector2i(-20, 0) or hit.normal != Vector2i(-1, 0) or hit.material != &"sand":
		printerr("  raycast returned %s, expected sand at (-20, 0) with normal (-1, 0)" % hit)
		return false
	return true


func test_particle_api_rejects_bad_input() -> bool:
	var world: PixelWorld = await _make_world()
	# The error the unknown material pushes is the expected behaviour, not a test failure.
	var unknown: int = world.spawn_particles(&"nope", Vector2.ZERO, Vector2.ZERO, 5, 0.0)
	var negative: int = world.spawn_particles(&"sand", Vector2.ZERO, Vector2.ZERO, -5, 0.0)
	var wild: int = world.spawn_particles(&"sand", Vector2.ZERO, Vector2(INF, NAN), 5, 0.0)
	world.step(1)
	var no_blast: int = world.explode(Vector2i(0, 0), 0, 100.0)
	var no_mask: Dictionary = world.raycast(Vector2(0, 0), Vector2(0, 1000), 0)
	world.free()
	if unknown != 0 or negative != 0 or wild <= 0 or no_blast != 0 or not no_mask.is_empty():
		printerr("  unknown %d, negative %d, wild %d, radius-0 explode %d, mask-0 raycast %s" % [unknown, negative, wild, no_blast, no_mask])
		return false
	return true


func test_particle_api_rejects_unbounded_input() -> bool:
	var world: PixelWorld = await _make_world()
	# The errors these push are the expected behaviour, not test failures.
	var huge_blast: int = world.explode(Vector2i(0, 0), 1000, 100.0)
	var nan_spawn: int = world.spawn_particles(&"sand", Vector2(NAN, NAN), Vector2.ZERO, 5, 0.0)
	var inf_sparks: int = world.spawn_visual_particles(Color.RED, Vector2(INF, 0), Vector2.ZERO, 5, 0.0, 1.0)
	var nan_ray: Dictionary = world.raycast(Vector2(NAN, 0), Vector2(0, 100))
	world.free()
	if huge_blast != 0 or nan_spawn != 0 or inf_sparks != 0 or not nan_ray.is_empty():
		printerr("  radius-1000 explode %d, NaN spawn %d, INF sparks %d, NaN raycast %s" % [huge_blast, nan_spawn, inf_sparks, nan_ray])
		return false
	return true


# --- rigid bodies (Plan 4) -------------------------------------------------------------------

# A w x h texture, opaque everywhere, or a disc of that diameter when `disc` is set.
func _body_texture(w: int, h: int, colour: Color = Color.WHITE, disc: bool = false) -> ImageTexture:
	var image := Image.create(w, h, false, Image.FORMAT_RGBA8)
	for y: int in h:
		for x: int in w:
			var inside := true
			if disc:
				var dx := x + 0.5 - w / 2.0
				var dy := y + 0.5 - h / 2.0
				inside = dx * dx + dy * dy <= (w / 2.0) * (w / 2.0)
			image.set_pixel(x, y, colour if inside else Color(0, 0, 0, 0))
	return ImageTexture.create_from_image(image)


func _make_body(w: int, h: int, material: StringName, at: Vector2, disc: bool = false) -> PixelRigidBody:
	var body := PixelRigidBody.new()
	body.texture = _body_texture(w, h, Color.WHITE, disc)
	body.pixel_material = material
	if disc:
		body.shape = PixelRigidBody.SHAPE_CIRCLE
	body.position = at
	root.add_child(body)
	return body


# Bodies and joints register on their first physics frame.
func _register() -> void:
	await physics_frame
	await physics_frame


func test_body_falls_and_rests() -> bool:
	var world: PixelWorld = await _make_world()
	world.simulating = false
	var body := _make_body(12, 12, &"wood", Vector2(0, 200))
	var hits := [0]
	body.hit.connect(func(_other: Node, _point: Vector2, _normal: Vector2, _speed: float) -> void: hits[0] += 1)
	await _register()
	world.step(240)
	var y := body.position.y
	var bodies: int = world.get_stats().bodies
	world.free()
	body.free()
	if absf(y - 250.0) > 2.0 or hits[0] == 0 or bodies != 1:
		printerr("  body rests at y %.2f (expected 250), %d hits, %d bodies" % [y, hits[0], bodies])
		return false
	return true


func test_body_bad_definitions() -> bool:
	var world: PixelWorld = await _make_world()
	var nodes: Array[PixelRigidBody] = []
	var no_texture := PixelRigidBody.new()
	no_texture.pixel_material = &"wood"
	nodes.append(no_texture)
	var empty := PixelRigidBody.new()
	empty.texture = _body_texture(4, 4, Color(0, 0, 0, 0))
	empty.pixel_material = &"wood"
	nodes.append(empty)
	var unmapped := PixelRigidBody.new()
	unmapped.texture = _body_texture(4, 4, Color.RED)
	unmapped.palette = PixelRoomPalette.new()
	unmapped.palette.entries = {Color.BLUE: &"wood"}
	nodes.append(unmapped)
	var unknown := PixelRigidBody.new()
	unknown.texture = _body_texture(4, 4)
	unknown.pixel_material = &"unobtainium"
	nodes.append(unknown)
	for node: PixelRigidBody in nodes:
		root.add_child(node)
	# The errors these push are the expected behaviour, not test failures.
	await _register()
	var bodies: int = world.get_stats().bodies
	world.free()
	for node: PixelRigidBody in nodes:
		node.free()
	if bodies != 0:
		printerr("  %d bodies registered from bad definitions, expected 0" % bodies)
		return false
	return true


func test_body_free_order() -> bool:
	var world: PixelWorld = await _make_world()
	var body := _make_body(12, 12, &"wood", Vector2(0, 100))
	await _register()
	world.step(10)
	body.free()
	world.step(10)
	var left: int = world.get_stats().bodies

	# Now the world goes first, with a body and a joint still alive.
	var a := _make_body(12, 12, &"wood", Vector2(-6, 100))
	var b := _make_body(12, 12, &"wood", Vector2(6, 100))
	var joint := PixelRevoluteJoint.new()
	root.add_child(joint)
	joint.body_a = joint.get_path_to(a)
	joint.body_b = joint.get_path_to(b)
	await _register()
	world.free()
	await _register()
	a.linear_velocity = Vector2(10, 0)
	a.apply_impulse(Vector2(1, 0))
	joint.free()
	a.free()
	b.free()
	if left != 0:
		printerr("  %d bodies left after freeing the only one" % left)
		return false
	return true


func test_particles_do_not_land_inside_bodies() -> bool:
	var world: PixelWorld = await _make_world()
	world.simulating = false
	var body := _make_body(24, 24, &"wood", Vector2(0, 240))
	await _register()
	world.step(120)
	var centre := Vector2i(body.position.round())
	for i: int in 10:
		world.spawn_particles(&"sand", Vector2(-60, centre.y - 20 + 4 * i), Vector2(300, 0), 10, 0.2)
	world.step(120)
	var inside := 0
	for y: int in range(centre.y - 11, centre.y + 11):  # the body's interior, without its rim
		for x: int in range(centre.x - 11, centre.x + 11):
			if world.get_material_at(Vector2i(x, y)) == &"sand":
				inside += 1
	world.free()
	body.free()
	if inside > 0:
		printerr("  %d sand cells landed inside the body" % inside)
		return false
	return true


func test_raycast_reports_body() -> bool:
	var world: PixelWorld = await _make_world()
	world.simulating = false
	var body := _make_body(12, 12, &"wood", Vector2(0, 200))
	await _register()
	world.step(240)
	var hit: Dictionary = world.raycast(Vector2(-100, 250), Vector2(100, 250))
	world.free()
	var ok: bool = not hit.is_empty() and hit.collider == body and absf(hit.position.x + 6.0) <= 1.0
	body.free()
	if not ok:
		printerr("  raycast returned %s" % hit)
	return ok


func test_left_world() -> bool:
	var world: PixelWorld = await _make_world()
	world.simulating = false
	var body := _make_body(12, 12, &"wood", Vector2(0, 100))
	var left := [0]
	body.left_world.connect(func() -> void: left[0] += 1)
	await _register()
	world.step(2)
	body.teleport(Vector2(100000, 0), 0.0)
	world.step(2)
	var once: int = left[0]
	world.step(5)
	var still: int = left[0]
	world.free()
	body.free()
	if once != 1 or still != 1:
		printerr("  left_world fired %d then %d times, expected 1 and 1" % [once, still])
		return false
	return true


func test_wheel_joint_motor() -> bool:
	var world: PixelWorld = await _make_world()
	world.simulating = false
	var chassis := _make_body(40, 8, &"rock", Vector2(0, 220))
	var wheels: Array[PixelRigidBody] = [_make_body(10, 10, &"rock", Vector2(-14, 232), true), _make_body(10, 10, &"rock", Vector2(14, 232), true)]
	var joints: Array[PixelWheelJoint] = []
	for wheel: PixelRigidBody in wheels:
		var joint := PixelWheelJoint.new()
		joint.position = wheel.position
		root.add_child(joint)
		joint.body_a = joint.get_path_to(chassis)
		joint.body_b = joint.get_path_to(wheel)
		joint.enable_motor = true
		joint.motor_speed = 5.0
		joint.max_motor_torque = 1e6
		joints.append(joint)
	await _register()
	await _register()
	var start := chassis.position.x
	world.step(180)
	var moved := chassis.position.x - start
	world.free()
	for node: Node in [chassis, wheels[0], wheels[1], joints[0], joints[1]]:
		node.free()
	if moved < 40.0:
		printerr("  the car moved %.1f cells, expected >= 40" % moved)
		return false
	return true


# --- creatures (Plan 5) ----------------------------------------------------------------------

func _make_character(at: Vector2) -> PixelCharacter:
	var character := PixelCharacter.new()
	character.position = at
	root.add_child(character)
	return character


# A script extending the node with its own _physics_process (the CharacterBody2D way) still
# joins the world: joining must not live in the overridable callback.
func _scripted(base: String) -> GDScript:
	var script := GDScript.new()
	script.source_code = "extends %s\nvar ticks := 0\nfunc _physics_process(_delta: float) -> void:\n\tticks += 1\n" % base
	script.reload()
	return script


func test_script_subclasses_join_the_world() -> bool:
	var world: PixelWorld = await _make_world()
	var character := PixelCharacter.new()
	character.set_script(_scripted("PixelCharacter"))
	character.position = Vector2(0, 200)
	root.add_child(character)
	var body := PixelRigidBody.new()
	body.set_script(_scripted("PixelRigidBody"))
	body.texture = _body_texture(8, 8)
	body.pixel_material = &"wood"
	body.position = Vector2(40, 200)
	root.add_child(body)
	await _register()
	var stats: Dictionary = world.get_stats()
	var ticked: bool = character.ticks > 0 and body.ticks > 0
	world.free()
	character.free()
	body.free()
	if stats.creatures != 1 or stats.bodies != 1 or not ticked:
		printerr("  scripted nodes: %d creatures, %d bodies (expected 1 and 1), scripts ticked %s" % [stats.creatures, stats.bodies, ticked])
		return false
	return true


func test_character_walks_and_lands() -> bool:
	var world: PixelWorld = await _make_world()
	world.simulating = false
	var character := _make_character(Vector2(0, 200))
	await _register()
	world.step(60)
	var landed_y := character.position.y
	var on_floor := character.is_on_floor()
	for i: int in 30:
		character.velocity = Vector2(100, character.velocity.y)
		world.step(1)
	var walked := character.position.x
	var creatures: int = world.get_stats().creatures
	world.free()
	character.free()
	if absf(landed_y - 248.0) > 1.0 or not on_floor or walked < 40.0 or creatures != 1:
		printerr("  landed at y %.2f (248), on floor %s, walked to x %.1f (>= 40), %d creatures" % [landed_y, on_floor, walked, creatures])
		return false
	return true


func test_character_jumps_and_hits_ceiling() -> bool:
	var world: PixelWorld = await _make_world()
	world.simulating = false
	world.fill_rect(Rect2i(-20, 215, 40, 5), &"rock")
	var character := _make_character(Vector2(0, 240))
	await _register()
	world.step(30)
	character.velocity = Vector2(0, -400)
	var ceiling := false
	for i: int in 10:
		world.step(1)
		ceiling = ceiling or character.is_on_ceiling()
	var y := character.position.y
	world.free()
	character.free()
	if not ceiling or y < 228.0:
		printerr("  hit the ceiling: %s, y %.2f (expected >= 228, under the rock)" % [ceiling, y])
		return false
	return true


func test_character_swims() -> bool:
	var world: PixelWorld = await _make_world()
	world.simulating = false
	world.fill_rect(Rect2i(-60, 200, 120, 56), &"water")
	var character := _make_character(Vector2(0, 150))
	await _register()
	world.step(60)
	var ok := character.is_in_liquid() and character.get_liquid_material() == &"water" and character.get_submerged_fraction() > 0.5
	var report := "in liquid %s, material %s, submerged %.2f" % [character.is_in_liquid(), character.get_liquid_material(), character.get_submerged_fraction()]
	world.free()
	character.free()
	if not ok:
		printerr("  " + report)
	return ok


func test_character_free_order() -> bool:
	var world: PixelWorld = await _make_world()
	var first := _make_character(Vector2(0, 200))
	await _register()
	world.step(5)
	first.free()
	world.step(5)
	var second := _make_character(Vector2(20, 200))
	await _register()
	world.free()
	await _register()
	second.velocity = Vector2(10, 0)
	second.apply_impulse(Vector2(1, 0))
	var still := second.is_on_floor()
	second.free()
	return not still


func test_character_pushes_body() -> bool:
	var world: PixelWorld = await _make_world()
	world.simulating = false
	var crate := _make_body(12, 12, &"wood", Vector2(30, 244))
	var character := _make_character(Vector2(0, 248))
	await _register()
	world.step(20)
	var start := crate.position.x
	for i: int in 60:
		character.velocity = Vector2(100, character.velocity.y)
		world.step(1)
	var moved := crate.position.x - start
	world.free()
	crate.free()
	character.free()
	if moved < 5.0:
		printerr("  the crate moved %.1f cells, expected >= 5" % moved)
		return false
	return true


# --- generated world (Plan 2) --------------------------------------------------------------

# `target` must already be in the tree; the world's _ready blocks once to fill the active zone.
func _make_generated_world(target: Node2D) -> PixelWorld:
	var world: PixelWorld = PixelWorld.new()
	world.material_set = load("res://materials/default_set.tres")
	world.generator = load("res://worldgen/default_generator.tres")
	world.worker_threads = 1
	world.generator_threads = 2
	# Off from the start: the first frame can run physics ticks, and a settling tick rewrites
	# generated cells, which the cell-comparison test would then see as a difference.
	world.simulating = false
	world.follow_target = target.get_path()
	root.add_child(world)
	await process_frame
	return world


func _await_ready(world: PixelWorld, rect: Rect2i, max_frames: int = 600) -> bool:
	for i: int in max_frames:
		if world.is_area_ready(rect):
			return true
		await process_frame
	return world.is_area_ready(rect)


# The world cells covered by the (2*radius+1)-chunk zone around `cell`.
func _zone_rect(cell: Vector2i, radius: Vector2i) -> Rect2i:
	return Rect2i(cell.x - radius.x * 64, cell.y - radius.y * 64, (2 * radius.x + 1) * 64, (2 * radius.y + 1) * 64)


func test_generated_world_active_zone_ready() -> bool:
	var target: Node2D = Node2D.new()
	root.add_child(target)
	var world: PixelWorld = await _make_generated_world(target)
	var ready: bool = world.is_area_ready(_zone_rect(Vector2i.ZERO, Vector2i(8, 5)))
	# _ready fills the active zone synchronously; the sky cell is in the loaded zone (chunk
	# (0,-8)), which streams in normally, so wait for its chunk rather than the first frame.
	await _await_ready(world, Rect2i(0, -500, 1, 1))
	var sky: StringName = world.get_material_at(Vector2i(0, -500))
	world.free()
	target.free()
	if not ready or sky != &"air":
		printerr("  active zone ready=%s, (0,-500) is '%s'" % [ready, sky])
		return false
	return true


func test_far_chunks_discarded() -> bool:
	var target: Node2D = Node2D.new()
	root.add_child(target)
	var world: PixelWorld = await _make_generated_world(target)
	target.position = Vector2(20000, 0)
	var ready: bool = await _await_ready(world, _zone_rect(Vector2i(20000, 0), Vector2i(8, 5)))
	await process_frame
	await process_frame
	var origin: bool = world.is_chunk_loaded(Vector2i(0, 0))
	world.free()
	target.free()
	if not ready or origin:
		printerr("  far zone ready=%s, origin chunk loaded=%s" % [ready, origin])
		return false
	return true


func test_regenerated_chunk_identical() -> bool:
	var target: Node2D = Node2D.new()
	root.add_child(target)
	var world: PixelWorld = await _make_generated_world(target)
	world.simulating = false
	var before: Array[StringName] = []
	for y: int in range(4 * 64, 5 * 64):
		for x: int in range(0, 64):
			before.append(world.get_material_at(Vector2i(x, y)))

	target.position = Vector2(20000, 0)
	for i: int in 600:
		if not world.is_chunk_loaded(Vector2i(0, 4)):
			break
		await process_frame
	var unloaded: bool = not world.is_chunk_loaded(Vector2i(0, 4))

	target.position = Vector2.ZERO
	var ready: bool = await _await_ready(world, Rect2i(0, 4 * 64, 64, 64))

	var different := 0
	var index := 0
	for y: int in range(4 * 64, 5 * 64):
		for x: int in range(0, 64):
			if world.get_material_at(Vector2i(x, y)) != before[index]:
				different += 1
			index += 1
	world.free()
	target.free()
	if not unloaded or not ready or different != 0:
		printerr("  unloaded=%s ready=%s, %d of 4096 cells differ" % [unloaded, ready, different])
		return false
	return true


# The hole gate: `view_missing` is the spec's hole detector, so it must go non-zero when the
# viewport covers unloaded chunks. A current Camera2D puts the canvas transform over the far
# point, where nothing is loaded yet: the 480x270 viewport covers 9x5 chunks and one frame
# inserts at most max_inserts_per_frame (16).
func test_view_missing_nonzero_then_zero() -> bool:
	var target: Node2D = Node2D.new()
	root.add_child(target)
	var camera: Camera2D = Camera2D.new()
	root.add_child(camera)
	camera.make_current()
	var world: PixelWorld = await _make_generated_world(target)
	target.position = Vector2(100000, 0)
	camera.position = target.position
	await process_frame
	var jumping: int = world.get_stats()["view_missing"]
	var ready: bool = await _await_ready(world, _zone_rect(Vector2i(100000, 0), Vector2i(5, 3)))
	var settled: int = world.get_stats()["view_missing"]
	world.free()
	target.free()
	camera.free()
	if jumping <= 0 or not ready or settled != 0:
		printerr("  view_missing was %d after the jump (expected > 0), view ready=%s, %d once ready" % [jumping, ready, settled])
		return false
	return true


func test_teleport_and_freed_target() -> bool:
	var target: Node2D = Node2D.new()
	root.add_child(target)
	var world: PixelWorld = await _make_generated_world(target)
	target.position = Vector2(100000, 3000)
	var chunk_rect := Rect2i(1562 * 64, 46 * 64, 64, 64)
	# Nothing has been requested yet, so the far area is deterministically unloaded.
	var not_ready: bool = not world.is_area_ready(chunk_rect)
	var unloaded_read: StringName = world.get_material_at(Vector2i(100000, 3000))
	var ready: bool = await _await_ready(world, chunk_rect)
	var origin: bool = world.is_chunk_loaded(Vector2i(0, 0))
	world.simulating = true
	var tick_before: int = world.get_stats()["tick"]
	target.free()
	await physics_frame
	await process_frame
	await physics_frame
	await process_frame
	var tick_after: int = world.get_stats()["tick"]
	world.free()
	if not not_ready or unloaded_read != &"" or not ready or origin or tick_after <= tick_before:
		printerr("  before-frame ready=%s read='%s', after ready=%s origin=%s, ticks %d -> %d" % [not_ready, unloaded_read, ready, origin, tick_before, tick_after])
		return false
	return true


func test_bad_zone_values_clamped() -> bool:
	var target: Node2D = Node2D.new()
	root.add_child(target)
	var world: PixelWorld = await _make_generated_world(target)
	world.active_radius = Vector2i(-3, -3)
	world.loaded_margin = -5
	world.discard_hysteresis = -1
	for i: int in 3:
		await process_frame
	var loaded: int = world.get_stats()["chunks_loaded"]
	var centre_ready: bool = world.is_area_ready(Rect2i(0, 0, 64, 64))
	world.free()
	target.free()
	if loaded < 1 or not centre_ready:
		printerr("  chunks_loaded=%d, centre chunk ready=%s" % [loaded, centre_ready])
		return false
	return true


func test_free_world_while_streaming() -> bool:
	var target: Node2D = Node2D.new()
	root.add_child(target)
	var world: PixelWorld = await _make_generated_world(target)
	target.position = Vector2(50000, 0)
	await process_frame  # the request is published and the workers are busy
	world.free()  # joins them; no use-after-free of the generator or the table
	await process_frame
	await process_frame
	target.free()
	return true


func test_bad_reference_reports_name() -> bool:
	var wood: PixelMaterial = PixelMaterial.new()
	wood.name = &"wood"
	wood.material_class = PixelMaterial.CLASS_SOLID
	wood.burns_into = &"ash"
	var mats: Array[PixelMaterial] = [wood]
	var mat_set: PixelMaterialSet = PixelMaterialSet.new()
	mat_set.materials = mats
	var error: String = mat_set.validate()
	if not error.contains("ash") or not error.contains("wood"):
		printerr("  validate() said: '" + error + "'")
		return false
	return true


# The plan's fixed-step requirement (review focus #5): one physics tick per frame at 60 Hz,
# with at most 3 catch-up steps per rendered frame.
func test_physics_settings_pin_fixed_step() -> bool:
	var steps: int = ProjectSettings.get_setting("physics/common/max_physics_steps_per_frame")
	var tps: int = ProjectSettings.get_setting("physics/common/physics_ticks_per_second")
	if steps != 3 or tps != 60:
		printerr("  physics settings are max_physics_steps_per_frame=%d, physics_ticks_per_second=%d" % [steps, tps])
		return false
	return true


# End-to-end over the shipped data: acid + rock has both *_into unset in acid_rock.tres, which
# the plan reads as "-> air". An unset product used to spawn material id 65535 (NO_MAT): such a
# cell reports "" from get_material_at (the "unloaded" answer) and indexes the renderer's
# palette out of bounds. This drives the real material set through PixelWorld::step.
func test_acid_on_rock_leaves_air() -> bool:
	var world: PixelWorld = await _make_world()
	# The arena is Rect2i(-512,-256,1024,512) and everything outside it is rock, so the sealed
	# rock below the top edge is the reaction's rock. A pocket of acid buried in it cannot
	# move: the only thing that can happen to those cells is the reaction.
	var acid_cells: Array[Vector2i] = []
	for x: int in range(0, 4):
		acid_cells.append(Vector2i(x, 262))
	for cell: Vector2i in acid_cells:
		world.set_cell(cell, &"acid")
	# A cell of a known material next to the pocket, still there at the end: without it a
	# world that turned everything into air would pass the acid check.
	var keep: Vector2i = Vector2i(10, 262)
	world.set_cell(keep, &"wood")
	world.step(300)
	await process_frame  # render the changed chunks: write_chunk_image over their ids

	var failed := ""
	for cell: Vector2i in acid_cells:
		var mat: StringName = world.get_material_at(cell)
		if mat != &"air":
			failed += " (%d,%d)=%s" % [cell.x, cell.y, mat if mat != &"" else "<empty: NO_MAT>"]
	var kept: StringName = world.get_material_at(keep)
	var tick: int = world.get_stats()["tick"]
	world.free()
	if failed != "":
		printerr("  reacted acid cells are not air:" + failed)
		return false
	if kept != &"wood":
		printerr("  the untouched wood cell at %s reports '%s'" % [keep, kept])
		return false
	if tick < 300:
		printerr("  only %d ticks were simulated, expected 300" % tick)
		return false
	return true


func test_default_generator_validates() -> bool:
	var generator: PixelWorldGenerator = load("res://worldgen/default_generator.tres")
	if generator == null:
		printerr("  worldgen/default_generator.tres did not load")
		return false
	var error: String = generator.validate(load("res://materials/default_set.tres"))
	if error != "":
		printerr("  validate() said: '" + error + "'")
	return error == ""


# The starter biome set keeps the generator's terrain config valid, so a bad room is what
# compile reports (a lone forest biome would fail its open-ended last band first).
func _make_room_generator(room: PixelRoom, palette: PixelRoomPalette) -> PixelWorldGenerator:
	var generator: PixelWorldGenerator = PixelWorldGenerator.new()
	var biomes: Array[PixelBiome] = []
	for biome_name: String in ["forest", "desert", "underground", "caverns", "deep"]:
		biomes.append(load("res://worldgen/%s.tres" % biome_name))
	generator.biomes = biomes
	var rooms: Array[PixelRoom] = [room]
	generator.rooms = rooms
	generator.room_palette = palette
	return generator


func _colour_room_image(format: int) -> Image:
	var image: Image = Image.create_empty(4, 4, false, format)
	image.fill(Color8(0, 0, 0, 255))
	image.set_pixel(2, 1, Color8(18, 52, 86, 255))
	return image


func _room_palette(colours: Array) -> PixelRoomPalette:
	var palette: PixelRoomPalette = PixelRoomPalette.new()
	var entries: Dictionary = {}
	for colour in colours:
		entries[colour] = &"air"
	palette.entries = entries
	return palette


func test_room_colour_missing_reported() -> bool:
	var room: PixelRoom = PixelRoom.new()
	room.image = ImageTexture.create_from_image(_colour_room_image(Image.FORMAT_RGBA8))
	var palette: PixelRoomPalette = _room_palette([Color8(0, 0, 0, 255)])  # omits the blue pixel
	var error: String = _make_room_generator(room, palette).validate(load("res://materials/default_set.tres"))
	if not error.contains("(2, 1)") or not error.contains("#123456ff"):
		printerr("  validate() said: '" + error + "'")
		return false
	return true


func test_room_without_image_reported() -> bool:
	var room: PixelRoom = PixelRoom.new()
	var error: String = _make_room_generator(room, PixelRoomPalette.new()).validate(load("res://materials/default_set.tres"))
	if not error.contains("room 0 has no image"):
		printerr("  validate() said: '" + error + "'")
		return false
	return true


func test_rgb8_room_image_accepted() -> bool:
	var room: PixelRoom = PixelRoom.new()
	room.image = ImageTexture.create_from_image(_colour_room_image(Image.FORMAT_RGB8))
	var palette: PixelRoomPalette = _room_palette([Color8(0, 0, 0, 255), Color8(18, 52, 86, 255)])
	var error: String = _make_room_generator(room, palette).validate(load("res://materials/default_set.tres"))
	if error != "":
		printerr("  validate() said: '" + error + "'")
	return error == ""
