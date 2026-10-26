extends Node2D

# Lighting demo: a 1024x512 rock arena with a torch-carrying player, a lava pool and a wood
# pillar in a room on the right, and a 6-thick rock wall sealing the corridor between them.
# A/D walk, W jumps (and swims up in liquid), E explodes at the mouse, F sets fire at the
# mouse, L toggles lighting. The camera follows the player.

const ARENA := Rect2i(-512, -256, 1024, 512)
const WALK_SPEED := 80.0
const JUMP_SPEED := 300.0
const SWIM_SPEED := 120.0
const EXPLODE_RADIUS := 12
const EXPLODE_POWER := 300.0
const FIRE_RADIUS := 3
const PLAYER_START := Vector2(-60, -20)
# The corridor between the player's cave (left) and the lava room (right).
const CAVE := Vector2i(-300, -40)
const CAVE_RADIUS := 45
const CORRIDOR := Rect2i(-300, -70, 420, 100)
const LAVA_ROOM := Rect2i(124, -90, 196, 120)
const WALL := Rect2i(118, -90, 6, 120)
const LAVA_POOL := Rect2i(132, 0, 40, 30)
const WOOD_PILLAR := Rect2i(200, -60, 14, 90)

@onready var world: PixelWorld = $PixelWorld
@onready var camera: Camera2D = $Camera2D
@onready var info: Label = $UI/Label

var player: PixelCharacter
var torch: PixelLight
var built := false


func _ready() -> void:
	# The arena is painted once every chunk under it has streamed in; painting a missing
	# chunk would silently drop those cells.
	while not world.is_area_ready(ARENA):
		await get_tree().process_frame
	_build()
	built = true


func _build() -> void:
	world.fill_rect(ARENA, &"rock")
	world.fill_circle(CAVE, CAVE_RADIUS, &"air")
	world.fill_rect(CORRIDOR, &"air")
	world.fill_rect(LAVA_ROOM, &"air")
	world.fill_rect(WALL, &"rock")
	world.fill_rect(LAVA_POOL, &"lava")
	world.fill_rect(WOOD_PILLAR, &"wood")

	player = PixelCharacter.new()
	player.position = PLAYER_START
	var sprite := ColorRect.new()
	sprite.color = Color(0.75, 0.8, 0.9)
	sprite.size = Vector2(player.hitbox_size)
	sprite.position = -sprite.size / 2.0
	player.add_child(sprite)
	torch = PixelLight.new()
	torch.color = Color(1.0, 0.8, 0.5)
	torch.energy = 1.6
	torch.radius = 60.0
	torch.flicker = 0.3
	player.add_child(torch)
	add_child(player)


func _process(_delta: float) -> void:
	if player:
		camera.position = player.global_position
	var stats: Dictionary = world.get_stats()
	var count: int = stats.chunks_loaded
	info.text = "[A/D] walk  [W] jump  [E] explode  [F] fire  [L] lighting\nlights %d   light cpu %.2f ms   gpu %.2f ms\nchunks %d   sim %.2f ms" % [
		stats.light_spots, stats.light_cpu_ms, stats.light_gpu_ms, count, stats.sim_ms]


func _physics_process(_delta: float) -> void:
	if not player:
		return
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


func _unhandled_input(event: InputEvent) -> void:
	if not (event is InputEventKey) or not event.pressed or event.echo:
		return
	match event.physical_keycode:
		KEY_E:
			world.explode(_mouse_cell(), EXPLODE_RADIUS, EXPLODE_POWER)
		KEY_F:
			world.fill_circle(_mouse_cell(), FIRE_RADIUS, &"fire")
		KEY_L:
			world.lighting_enabled = not world.lighting_enabled


func _mouse_cell() -> Vector2i:
	var at := get_global_mouse_position()
	return Vector2i(floori(at.x), floori(at.y))
