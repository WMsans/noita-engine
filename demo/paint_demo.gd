extends Node2D

# Paint demo: WASD pans the camera that PixelWorld follows, left mouse paints the selected
# material in a radius-6 circle, right mouse paints air, 1-0 pick one of the ten starter
# materials, H sleeps the main thread for the hitch check.

const MATERIALS: Array[StringName] = [&"rock", &"wood", &"sand", &"water", &"oil", &"lava", &"acid", &"fire", &"steam", &"smoke"]
const BRUSH_RADIUS := 6
const PAN_SPEED := 300.0  # cells per second

@onready var world: PixelWorld = $PixelWorld
@onready var camera: Camera2D = $Camera2D
@onready var info: Label = $UI/Label

var selected := 2  # sand


func _process(delta: float) -> void:
	_pan(delta)
	_paint()
	var stats: Dictionary = world.get_stats()
	info.text = "material: %s (%d)   brush: %d\n[1-0] pick  [LMB] paint  [RMB] air  [WASD] pan  [H] hitch\ntick %d   sim %.2f ms   upload %.2f ms\nchunks %d   simulated %d   cells %d" % [
		MATERIALS[selected], selected + 1, BRUSH_RADIUS,
		stats.tick, stats.sim_ms, stats.upload_ms,
		stats.chunks_loaded, stats.chunks_simulated, stats.cells_updated]


func _pan(delta: float) -> void:
	var direction := Vector2.ZERO
	if Input.is_key_pressed(KEY_A):
		direction.x -= 1.0
	if Input.is_key_pressed(KEY_D):
		direction.x += 1.0
	if Input.is_key_pressed(KEY_W):
		direction.y -= 1.0
	if Input.is_key_pressed(KEY_S):
		direction.y += 1.0
	if direction != Vector2.ZERO:
		camera.position += direction.normalized() * PAN_SPEED * delta


func _paint() -> void:
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
		world.fill_circle(_mouse_cell(), BRUSH_RADIUS, MATERIALS[selected])
	elif Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		world.fill_circle(_mouse_cell(), BRUSH_RADIUS, &"air")


func _mouse_cell() -> Vector2i:
	var at := get_global_mouse_position()
	return Vector2i(floori(at.x), floori(at.y))


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	if key.physical_keycode == KEY_H:  # debug: block the main thread for half a second
		OS.delay_msec(500)
		return
	var index := _material_index(key.physical_keycode)
	if index >= 0:
		selected = index


func _material_index(keycode: Key) -> int:
	if keycode >= KEY_1 and keycode <= KEY_9:
		return keycode - KEY_1
	if keycode == KEY_0:
		return 9
	return -1
