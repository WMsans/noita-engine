class_name CarController extends Node2D

# The demo car: a chassis and two motorised wheels joined by PixelWheelJoints, built from the
# exports below at the controller's position. drive() and set_braking() are the only controls;
# `chassis` is the body to follow with a camera.

@export var chassis_size := Vector2i(40, 8)
@export var chassis_colour := Color(0.75, 0.2, 0.15)
@export var chassis_material: StringName = &"wood"
@export var wheel_diameter := 12
@export var wheel_colour := Color(0.15, 0.15, 0.15)
@export var wheel_material: StringName = &"rock"
@export var wheel_offsets := PackedVector2Array([Vector2(-14, 10), Vector2(14, 10)])
@export var wheel_friction := 1.5  # rubber: enough grip for a 45-degree sand pile
@export var motor_speed := 12.0  # rad/s
@export var motor_torque := 3e6  # Box2D works in cells, so torques are large numbers
@export var wheel_frequency := 5.0

var chassis: PixelRigidBody
var _wheels: Array[PixelWheelJoint] = []
var _direction := 0.0
var _braking := false


func _ready() -> void:
	chassis = _add_body(TexturesUtil.box(chassis_size.x, chassis_size.y, chassis_colour), chassis_material, Vector2.ZERO)
	for offset: Vector2 in wheel_offsets:
		var wheel := _add_body(TexturesUtil.disc(wheel_diameter, wheel_colour), wheel_material, offset, PixelRigidBody.SHAPE_CIRCLE)
		wheel.friction = wheel_friction
		var joint := PixelWheelJoint.new()
		joint.position = offset
		add_child(joint)
		joint.body_a = joint.get_path_to(chassis)
		joint.body_b = joint.get_path_to(wheel)
		joint.max_motor_torque = motor_torque
		joint.frequency = wheel_frequency
		_wheels.append(joint)


# direction is -1..1, multiplied by motor_speed. The brake holds whichever was called last.
func drive(direction: float) -> void:
	_direction = direction
	_apply_motors()


func set_braking(on: bool) -> void:
	_braking = on
	_apply_motors()


func _apply_motors() -> void:
	for joint: PixelWheelJoint in _wheels:
		joint.enable_motor = _braking or _direction != 0.0
		joint.motor_speed = 0.0 if _braking else _direction * motor_speed


func _add_body(texture: Texture2D, material: StringName, at: Vector2, shape: int = PixelRigidBody.SHAPE_HULL) -> PixelRigidBody:
	var body := PixelRigidBody.new()
	body.texture = texture
	body.pixel_material = material
	body.shape = shape
	body.position = at
	add_child(body)
	return body
