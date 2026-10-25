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
