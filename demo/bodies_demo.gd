extends Node2D

# Rigid-body demo on the generated world. Left / right drive the car (Space brakes), C drops
# a crate and B a boulder at the mouse, S drops a sand pile and Q a water blob, middle mouse
# explodes. The camera follows the car.

const MOTOR_SPEED := 12.0  # rad/s
const MOTOR_TORQUE := 3e6  # Box2D works in cells, so torques are large numbers; enough for a 45-degree dune
const EXPLODE_RADIUS := 12
const EXPLODE_POWER := 300.0

@onready var world: PixelWorld = $PixelWorld
@onready var camera: Camera2D = $Camera2D
@onready var info: Label = $UI/Label

var chassis: PixelRigidBody
var wheels: Array[PixelWheelJoint] = []


func _ready() -> void:
	var ground: Dictionary = world.raycast(Vector2(0.5, -150), Vector2(0.5, 450), PixelWorld.RAY_SOLID | PixelWorld.RAY_POWDER)
	var ground_y: float = ground.position.y if not ground.is_empty() else 0.0
	_build_car(Vector2(0, ground_y - 30))
	camera.position = chassis.position


func _process(_delta: float) -> void:
	camera.position = chassis.global_position
	var drive := 0.0
	if Input.is_key_pressed(KEY_RIGHT):
		drive += MOTOR_SPEED
	if Input.is_key_pressed(KEY_LEFT):
		drive -= MOTOR_SPEED
	var braking := Input.is_key_pressed(KEY_SPACE)
	for joint: PixelWheelJoint in wheels:
		joint.enable_motor = braking or drive != 0.0
		joint.motor_speed = drive
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
			add_body(texture_box(12, 12, Color(0.55, 0.35, 0.18)), &"wood", get_global_mouse_position())
		KEY_B:
			add_body(texture_disc(14, Color(0.45, 0.45, 0.48)), &"rock", get_global_mouse_position(), PixelRigidBody.SHAPE_CIRCLE)
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


func _build_car(at: Vector2) -> void:
	chassis = add_body(texture_box(40, 8, Color(0.75, 0.2, 0.15)), &"wood", at)
	for dx: float in [-14.0, 14.0]:
		var wheel := add_body(texture_disc(12, Color(0.15, 0.15, 0.15)), &"rock", at + Vector2(dx, 10), PixelRigidBody.SHAPE_CIRCLE)
		wheel.friction = 1.5  # rubber: enough grip for a 45-degree sand pile
		var joint := PixelWheelJoint.new()
		joint.position = wheel.position
		add_child(joint)
		joint.body_a = joint.get_path_to(chassis)
		joint.body_b = joint.get_path_to(wheel)
		joint.max_motor_torque = MOTOR_TORQUE
		joint.frequency = 5.0
		wheels.append(joint)


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


func _mouse_cell() -> Vector2i:
	var at := get_global_mouse_position()
	return Vector2i(floori(at.x), floori(at.y))
