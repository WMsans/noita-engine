extends Node2D

# Paint demo: WASD pans the camera that PixelWorld follows (hold Shift for 4x), left mouse
# paints the selected material in a radius-6 circle, right mouse paints air, 1-0 pick one of
# the ten starter materials, H sleeps the main thread for the hitch check. Middle mouse
# explodes, F sprays visual sparks and G sprays water particles at the mouse, C drops a crate
# and B a boulder there.

const MATERIALS: Array[StringName] = [&"rock", &"wood", &"sand", &"water", &"oil", &"lava", &"acid", &"fire", &"steam", &"smoke"]
const BRUSH_RADIUS := 6
const PAN_SPEED := 300.0  # cells per second
const FAST_PAN := 4.0
const EXPLODE_RADIUS := 12
const EXPLODE_POWER := 300.0  # cells per second at the centre

@onready var world: PixelWorld = $PixelWorld
@onready var camera: Camera2D = $Camera2D
@onready var info: Label = $UI/Label

var selected := 2  # sand


func _ready() -> void:
	if world.generator == null:
		return
	# Start the view on the generated surface, wherever the hills put it.
	var ground: Dictionary = world.raycast(Vector2(0.5, -150), Vector2(0.5, 450), PixelWorld.RAY_SOLID | PixelWorld.RAY_POWDER)
	if not ground.is_empty():
		camera.position = Vector2(0, ground.position.y)


func _process(delta: float) -> void:
	_pan(delta)
	_paint()
	var stats: Dictionary = world.get_stats()
	info.text = "material: %s (%d)   brush: %d\n[1-0] pick  [LMB] paint  [RMB] air  [MMB] explode  [F] sparks  [G] water  [C] crate  [B] boulder  [WASD] pan  [Shift] fast  [H] hitch\ntick %d   sim %.2f ms   upload %.2f ms\nchunks %d   active %d   simulated %d   cells %d\ngen pending %d   gen %.2f ms   stream %.2f ms   view missing %d\nparticles %d / visual %d   particle %.2f ms   dropped %d" % [
		MATERIALS[selected], selected + 1, BRUSH_RADIUS,
		stats.tick, stats.sim_ms, stats.upload_ms,
		stats.chunks_loaded, stats.chunks_active, stats.chunks_simulated, stats.cells_updated,
		stats.gen_pending, stats.gen_ms, stats.stream_ms, stats.view_missing,
		stats.particles_material, stats.particles_visual, stats.particle_ms, stats.particles_dropped]


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
		var speed := PAN_SPEED * (FAST_PAN if Input.is_key_pressed(KEY_SHIFT) else 1.0)
		camera.position += direction.normalized() * speed * delta


func _paint() -> void:
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
		world.fill_circle(_mouse_cell(), BRUSH_RADIUS, MATERIALS[selected])
	elif Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		world.fill_circle(_mouse_cell(), BRUSH_RADIUS, &"air")


func _unhandled_input(event: InputEvent) -> void:
	var button := event as InputEventMouseButton
	if button and button.pressed and button.button_index == MOUSE_BUTTON_MIDDLE:
		world.explode(_mouse_cell(), EXPLODE_RADIUS, EXPLODE_POWER)


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
	if key.physical_keycode == KEY_F:
		world.spawn_visual_particles(Color.YELLOW, get_global_mouse_position(), Vector2(0, -150), 60, PI / 2, 0.6)
		return
	if key.physical_keycode == KEY_G:
		world.spawn_particles(&"water", get_global_mouse_position(), Vector2(0, -200), 40, 0.6)
		return
	if key.physical_keycode == KEY_C:
		add_body(texture_box(12, 12, Color(0.55, 0.35, 0.18)), &"wood", get_global_mouse_position())
		return
	if key.physical_keycode == KEY_B:
		add_body(texture_disc(14, Color(0.45, 0.45, 0.48)), &"rock", get_global_mouse_position(), PixelRigidBody.SHAPE_CIRCLE)
		return
	var index := _material_index(key.physical_keycode)
	if index >= 0:
		selected = index


func add_body(texture: Texture2D, material: StringName, at: Vector2, shape: int = PixelRigidBody.SHAPE_HULL) -> PixelRigidBody:
	var body := PixelRigidBody.new()
	body.texture = texture
	body.pixel_material = material
	body.shape = shape
	body.position = at
	add_child(body)
	return body


func texture_box(w: int, h: int, colour: Color) -> ImageTexture:
	var image := Image.create(w, h, false, Image.FORMAT_RGBA8)
	image.fill(colour)
	for x: int in w:
		image.set_pixel(x, 0, colour.darkened(0.3))
		image.set_pixel(x, h - 1, colour.darkened(0.3))
	for y: int in h:
		image.set_pixel(0, y, colour.darkened(0.3))
		image.set_pixel(w - 1, y, colour.darkened(0.3))
	return ImageTexture.create_from_image(image)


func texture_disc(diameter: int, colour: Color) -> ImageTexture:
	var image := Image.create(diameter, diameter, false, Image.FORMAT_RGBA8)
	var r := diameter / 2.0
	for y: int in diameter:
		for x: int in diameter:
			var dx := x + 0.5 - r
			var dy := y + 0.5 - r
			image.set_pixel(x, y, colour.lightened(0.15 * float(x < r)) if dx * dx + dy * dy <= r * r else Color(0, 0, 0, 0))
	return ImageTexture.create_from_image(image)


func _material_index(keycode: Key) -> int:
	if keycode >= KEY_1 and keycode <= KEY_9:
		return keycode - KEY_1
	if keycode == KEY_0:
		return 9
	return -1
