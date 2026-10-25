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
		if not call(method_name):
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
