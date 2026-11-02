extends Node2D

# Rigid-body demo on the generated world. Left / right drive the car (Space brakes), C drops
# a crate and B a boulder at the mouse, S drops a sand pile and Q a water blob, middle mouse
# explodes. The camera follows the car.

const EXPLODE_RADIUS := 12
const EXPLODE_POWER := 300.0

@onready var world: PixelWorld = $PixelWorld
@onready var camera: Camera2D = $Camera2D
@onready var info: Label = $UI/Label

var car: CarController


func _ready() -> void:
	var ground: Dictionary = world.raycast(Vector2(0.5, -150), Vector2(0.5, 450), PixelWorld.RAY_SOLID | PixelWorld.RAY_POWDER)
	var ground_y: float = ground.position.y if not ground.is_empty() else 0.0
	car = CarController.new()
	car.position = Vector2(0, ground_y - 30)
	add_child(car)
	camera.position = car.chassis.global_position


func _process(_delta: float) -> void:
	camera.position = car.chassis.global_position
	var direction := 0.0
	if Input.is_key_pressed(KEY_RIGHT):
		direction += 1.0
	if Input.is_key_pressed(KEY_LEFT):
		direction -= 1.0
	car.drive(direction)
	car.set_braking(Input.is_key_pressed(KEY_SPACE))
	var stats: Dictionary = world.get_stats()
	info.text = "[Left/Right] drive  [Space] brake  [C] crate  [B] boulder  [S] sand  [Q] water  [MMB] explode\nbodies %d awake %d   terrain bodies %d   shape builds %d\nphysics %.2f ms   shapes %.2f ms   stamp %.2f ms   sim %.2f ms   particles %d" % [
		stats.bodies, stats.bodies_awake, stats.terrain_bodies, stats.shape_builds,
		stats.physics_ms, stats.shape_ms, stats.stamp_ms, stats.sim_ms, stats.particles_material]


func _unhandled_input(event: InputEvent) -> void:
	var button := event as InputEventMouseButton
	if button and button.pressed and button.button_index == MOUSE_BUTTON_MIDDLE:
		world.explode(_mouse_cell(), EXPLODE_RADIUS, EXPLODE_POWER)
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	match key.physical_keycode:
		KEY_C:
			add_body(TexturesUtil.box(12, 12, Color(0.55, 0.35, 0.18)), &"wood", get_global_mouse_position())
		KEY_B:
			add_body(TexturesUtil.disc(14, Color(0.45, 0.45, 0.48)), &"rock", get_global_mouse_position(), PixelRigidBody.SHAPE_CIRCLE)
		KEY_S:
			world.fill_circle(_mouse_cell(), 10, &"sand")
		KEY_Q:
			world.fill_circle(_mouse_cell(), 10, &"water")


func add_body(texture: Texture2D, material: StringName, at: Vector2, shape: int = PixelRigidBody.SHAPE_HULL) -> PixelRigidBody:
	var body := PixelRigidBody.new()
	body.texture = texture
	body.pixel_material = material
	body.shape = shape
	body.position = at
	add_child(body)
	return body


func _mouse_cell() -> Vector2i:
	var at := get_global_mouse_position()
	return Vector2i(floori(at.x), floori(at.y))
