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
	if mat_set.materials.size() != 10 or mat_set.reactions.size() != 5:
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
	var expected: Array[StringName] = [&"rock", &"wood", &"sand", &"water", &"oil", &"lava", &"acid", &"fire", &"steam", &"smoke"]
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
	for key: String in ["sim_ms", "upload_ms", "chunks_loaded", "chunks_simulated", "cells_updated", "tick"]:
		if not stats.has(key):
			printerr("  get_stats() is missing '%s'; keys are %s" % [key, stats.keys()])
			return false
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
