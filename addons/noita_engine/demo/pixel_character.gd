class_name PixelCharacter
extends PixelRigidBody

## One character: an upright box that Box2D moves against other bodies and that
## `CharacterSystem` corrects against the cells every tick. Game code drives it like
## CharacterBody2D: read `velocity`, change it, write it back each physics frame. It draws
## nothing; add sprites as children. Units are cells and seconds.

## The hitbox. Changing it before the character joins changes the body.
@export var hitbox_size: Vector2i = Vector2i(8, 16):
	set(value):
		hitbox_size = value
		body_size = value

## Ledges up to this many cells high are walked up; it is also how far the feet reach down
## for the floor snap, and the bottom rows the cell proxies leave open.
@export var step_height: int = 3:
	set(value):
		step_height = value
		proxy_step_height = value

# What the last tick found. CharacterSystem writes these; the queries below read them.
var _floor := false
var _ceiling := false
var _wall := 0
var _submerged := 0.0
var _liquid: StringName = &""
var _was_on_floor := false

var _system: CharacterSystem


func _init() -> void:
	mass = 128.0
	friction = 0.6
	gravity_scale = 1.0
	terrain_collision = false  # the cells are CharacterSystem's job
	cell_proxies = true
	lock_rotation = true
	can_sleep = false  # corrected every tick, so it must never sleep
	body_size = hitbox_size
	proxy_step_height = step_height
	# Connected here, not in _ready: a subclass that overrides _ready still registers. Guarded
	# because setting a script on a live node runs _init again.
	if not joined.is_connected(_on_joined):
		joined.connect(_on_joined)
	if not tree_exiting.is_connected(_on_tree_exiting):
		tree_exiting.connect(_on_tree_exiting)


var velocity: Vector2:
	get:
		return linear_velocity
	set(value):
		linear_velocity = value


func is_on_floor() -> bool:
	return _floor


func is_on_ceiling() -> bool:
	return _ceiling


func is_on_wall() -> bool:
	return _wall != 0


## -1 against a wall on the left, 1 on the right, 0 while not against one.
func get_wall_side() -> int:
	return _wall


func is_in_liquid() -> bool:
	return _submerged > 0.0


func get_submerged_fraction() -> float:
	return _submerged


func get_liquid_material() -> StringName:
	return _liquid


func _on_joined() -> void:
	var world := get_world_node()
	if world == null:
		return
	_system = CharacterSystem.of(world)
	_system.add(self)


func _on_tree_exiting() -> void:
	if is_instance_valid(_system):
		_system.remove(self)
	_system = null
