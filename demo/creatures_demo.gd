extends "res://demo/bodies_demo.gd"

# Creatures on top of the bodies demo: A / D walk the player, W jumps (and swims up in liquid),
# 20 wanderers walk, turn at walls and hop. The camera follows the player; the car, crates,
# boulders, sand, water and explosions work as in the bodies demo.

const WALK_SPEED := 80.0
const JUMP_SPEED := 300.0
const SWIM_SPEED := 120.0
const WANDER_SPEED := 50.0
const WANDERERS := 20

var player: PixelCharacter
var wanderers: Array[PixelCharacter] = []


func _ready() -> void:
	super()
	var ground: Dictionary = world.raycast(Vector2(-60.5, -150), Vector2(-60.5, 300), PixelWorld.RAY_SOLID | PixelWorld.RAY_POWDER)
	var ground_y: float = ground.position.y if not ground.is_empty() else 0.0
	player = add_character(Vector2(-60, ground_y - 20), Color(0.3, 0.6, 1.0))
	for i: int in WANDERERS:
		var wanderer := add_character(Vector2(-200 + 20 * i, ground_y - 60), Color(0.9, 0.3, 0.3))
		wanderer.set_meta(&"dir", 1.0 if i % 2 == 0 else -1.0)
		wanderers.append(wanderer)


func _process(delta: float) -> void:
	super(delta)
	camera.position = player.global_position


func _physics_process(_delta: float) -> void:
	drive_player()
	for wanderer: PixelCharacter in wanderers:
		wander(wanderer)


func drive_player() -> void:
	var v := player.velocity
	v.x = 0.0
	if Input.is_key_pressed(KEY_A):
		v.x -= WALK_SPEED
	if Input.is_key_pressed(KEY_D):
		v.x += WALK_SPEED
	if Input.is_key_pressed(KEY_W):
		if player.is_in_liquid():
			v.y = -SWIM_SPEED
		elif player.is_on_floor():
			v.y = -JUMP_SPEED
	player.velocity = v


func wander(wanderer: PixelCharacter) -> void:
	var dir: float = wanderer.get_meta(&"dir")
	if wanderer.get_wall_side() == int(signf(dir)):
		dir = -dir
		wanderer.set_meta(&"dir", dir)
	var v := wanderer.velocity
	v.x = dir * WANDER_SPEED
	if wanderer.is_on_floor() and randf() < 1.0 / 90.0:
		v.y = -JUMP_SPEED * 0.6
	wanderer.velocity = v


func add_character(at: Vector2, colour: Color) -> PixelCharacter:
	var character := PixelCharacter.new()
	character.position = at
	var sprite := ColorRect.new()
	sprite.color = colour
	sprite.size = Vector2(character.hitbox_size)
	sprite.position = -sprite.size / 2.0
	character.add_child(sprite)
	add_child(character)
	return character
