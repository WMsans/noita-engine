class_name CharacterSystem
extends Node

## Runs every registered character's grid behaviour once per tick, for all of them at once.
## It is a child of its PixelWorld and listens to `bodies_stepped`, which fires right after the
## Box2D step and before the cell proxies are placed: the correction is therefore what the
## proxies and the cell sim see this frame, and the body's node transform catches up after.
##
## Every step is one batched C++ query over all characters (plus at most one more when a
## character has to be dug out), so the number of calls does not grow with the character count.
## The behaviour itself lives here, not in C++.

# Thresholds and limits, from the old C++ creature pass.
const SPLASH_SPEED := 60.0  # cells/s: entering liquid faster than this throws a splash
const KNOCK_SPEED := 2.0  # cells/tick: powder knocked out of the hitbox
const UNSTICK_UP := 64  # cells a buried character may rise
const UNSTICK_SIDE := 32  # cells it may then move sideways
const EDGE := 0.0001  # a box edge on a cell boundary covers no extra cell

const BLOCKS := PixelWorld.RAY_SOLID | PixelWorld.RAY_POWDER
const SOLID := PixelWorld.RAY_SOLID
const FLOOR := PixelWorld.SIDE_FLOOR
const CEILING := PixelWorld.SIDE_CEILING

## Time the last tick's handler took, in microseconds. The bench reports it.
var last_tick_usec := 0

var _world: PixelWorld
var _characters: Array[PixelCharacter] = []


## The system of `world`, created as its child on first use.
static func of(world: PixelWorld) -> CharacterSystem:
	for child: Node in world.get_children():
		if child is CharacterSystem:
			return child
	var system := CharacterSystem.new()
	system.name = "CharacterSystem"
	world.add_child(system)
	return system


func add(character: PixelCharacter) -> void:
	if not _characters.has(character):
		_characters.append(character)


func remove(character: PixelCharacter) -> void:
	_characters.erase(character)


func _ready() -> void:
	_world = get_parent() as PixelWorld
	if _world:
		_world.bodies_stepped.connect(_on_bodies_stepped)


func _on_bodies_stepped() -> void:
	var start := Time.get_ticks_usec()
	for i in range(_characters.size() - 1, -1, -1):
		if not is_instance_valid(_characters[i]):
			_characters.remove_at(i)
	var count := _characters.size()
	if count == 0:
		last_tick_usec = Time.get_ticks_usec() - start
		return

	var to_world := _world.get_global_transform().affine_inverse()
	var to_global := _world.get_global_transform()
	var sizes := PackedVector2Array()
	var prev := PackedVector2Array()
	var cur := PackedVector2Array()
	sizes.resize(count)
	prev.resize(count)
	cur.resize(count)
	for i: int in count:
		var character := _characters[i]
		sizes[i] = Vector2(character.body_size)
		prev[i] = to_world * character.get_previous_position()
		cur[i] = to_world * character.get_body_position()

	# 1. From where each box started the tick to where Box2D moved it, x first then y, one
	#    cell per sub-step: nothing tunnels, and a blocked box ends flush against the cells.
	var swept: Dictionary = _world.sweep_boxes(prev, cur, sizes, BLOCKS)
	if swept.is_empty():
		last_tick_usec = Time.get_ticks_usec() - start
		return
	var pos: PackedVector2Array = swept.position
	var stopped: PackedInt32Array = swept.stopped
	var sides: PackedInt32Array = swept.sides

	# 2. Step up. A box stopped on x while it was walking probes every raise up to
	#    `step_height`, both where it stands and one cell ahead, and takes the lowest free
	#    pair; a second sweep covers the rest of the distance at that height. At most one step
	#    per tick, which is enough at walking speed.
	var cands := PackedVector2Array()
	var cand_sizes := PackedVector2Array()
	var cand_owner := PackedInt32Array()
	var cand_height := PackedInt32Array()
	var stepping := PackedInt32Array()
	stepping.resize(count)
	for i: int in count:
		var character := _characters[i]
		var dx := cur[i].x - prev[i].x
		if stopped[i] & PixelWorld.STOPPED_X == 0 or dx == 0.0 or not character._was_on_floor:
			continue
		var ahead: float = pos[i].x + signf(dx) * minf(1.0, absf(dx))
		for k: int in range(1, character.step_height + 1):
			cands.append(Vector2(pos[i].x, pos[i].y - k))
			cands.append(Vector2(ahead, pos[i].y - k))
			cand_sizes.append(sizes[i])
			cand_sizes.append(sizes[i])
			cand_owner.append(i)
			cand_owner.append(i)
			cand_height.append(k)
			cand_height.append(k)
	if not cands.is_empty():
		var free: PackedByteArray = _world.probe_boxes(cands, cand_sizes, BLOCKS)
		for c: int in range(0, free.size(), 2):
			if free[c] == 0 and free[c + 1] == 0 and stepping[cand_owner[c]] == 0:
				stepping[cand_owner[c]] = cand_height[c]
	var step_from := PackedVector2Array()
	var step_to := PackedVector2Array()
	var step_sizes := PackedVector2Array()
	var step_owner := PackedInt32Array()
	for i: int in count:
		if stepping[i] == 0:
			continue
		step_owner.append(i)
		step_from.append(Vector2(pos[i].x, pos[i].y - stepping[i]))
		step_to.append(Vector2(cur[i].x, cur[i].y - stepping[i]))
		step_sizes.append(sizes[i])
	if not step_from.is_empty():
		var again: Dictionary = _world.sweep_boxes(step_from, step_to, step_sizes, BLOCKS)
		var again_pos: PackedVector2Array = again.position
		var again_stopped: PackedInt32Array = again.stopped
		var again_sides: PackedInt32Array = again.sides
		for j: int in step_owner.size():
			pos[step_owner[j]] = again_pos[j]
			stopped[step_owner[j]] = again_stopped[j]
			sides[step_owner[j]] = again_sides[j]

	# 3. Floor snap. A box that was on the floor, is not moving up and has no floor now
	#    probes 1..step_height cells down; the highest blocked offset drops it onto the floor,
	#    so it walks down slopes and small steps instead of hopping.
	var down := PackedVector2Array()
	var down_sizes := PackedVector2Array()
	var down_owner := PackedInt32Array()
	var down_height := PackedInt32Array()
	for i: int in count:
		if not _characters[i]._was_on_floor or cur[i].y < prev[i].y or sides[i] & FLOOR != 0:
			continue
		for k: int in range(1, _characters[i].step_height + 1):
			down.append(Vector2(pos[i].x, pos[i].y + k))
			down_sizes.append(sizes[i])
			down_owner.append(i)
			down_height.append(k)
	if not down.is_empty():
		var blocked: PackedByteArray = _world.probe_boxes(down, down_sizes, BLOCKS)
		for c: int in blocked.size():
			if blocked[c] == 0:
				continue
			var i := down_owner[c]
			if sides[i] & FLOOR != 0:
				continue  # a shorter candidate already snapped this one
			var below: float = floorf(pos[i].y + sizes[i].y * 0.5 - EDGE) + 1.0
			pos[i].y = below + down_height[c] - 1.0 - sizes[i].y * 0.5
			sides[i] = sides[i] | FLOOR

	# 4. What is in each box now: solid cells mean it is buried, powder gets knocked out,
	#    liquid is measured for the submersion and the splash.
	var scanned: Dictionary = _world.scan_boxes(pos, sizes)
	if scanned.is_empty():
		last_tick_usec = Time.get_ticks_usec() - start
		return
	var solids: PackedInt32Array = scanned.solid
	var powders: PackedInt32Array = scanned.powder
	var liquids: PackedInt32Array = scanned.liquid
	var materials: PackedStringArray = scanned.liquid_material

	# 5. Unstick. A box overlapping solid cells (painted over, or shoved in) rises 1..64
	#    cells; failing that it moves 1..32 cells to either side. Rare, so the long candidate
	#    lists are affordable, and they all go in one query per direction.
	var buried := PackedInt32Array()
	for i: int in count:
		if solids[i] > 0:
			buried.append(i)
	if not buried.is_empty():
		var freed := {}
		var ups := PackedVector2Array()
		var up_sizes := PackedVector2Array()
		var up_owner := PackedInt32Array()
		for i: int in buried:
			for k: int in range(1, UNSTICK_UP + 1):
				ups.append(Vector2(pos[i].x, pos[i].y - k))
				up_sizes.append(sizes[i])
				up_owner.append(i)
		var up_blocked: PackedByteArray = _world.probe_boxes(ups, up_sizes, SOLID)
		for c: int in up_blocked.size():
			var i := up_owner[c]
			if up_blocked[c] != 0 or freed.has(i):
				continue
			pos[i] = ups[c]
			freed[i] = true
		var sideways := PackedInt32Array()
		for i: int in buried:
			if not freed.has(i):
				sideways.append(i)
		if not sideways.is_empty():
			var across := PackedVector2Array()
			var across_sizes := PackedVector2Array()
			var across_owner := PackedInt32Array()
			for i: int in sideways:
				for k: int in range(1, UNSTICK_SIDE + 1):
					for dir: int in [-1, 1]:
						across.append(Vector2(pos[i].x + dir * k, pos[i].y))
						across_sizes.append(sizes[i])
						across_owner.append(i)
			var across_blocked: PackedByteArray = _world.probe_boxes(across, across_sizes, SOLID)
			for c: int in across_blocked.size():
				var i := across_owner[c]
				if across_blocked[c] != 0 or freed.has(i):
					continue
				pos[i] = across[c]
				freed[i] = true
		if not freed.is_empty():
			scanned = _world.scan_boxes(pos, sizes)  # measure again where they ended up
			powders = scanned.powder
			liquids = scanned.liquid
			materials = scanned.liquid_material

	# 6. Powder inside a box is knocked out as particles, top row first, thrown away from the
	#    centre; a box entering liquid faster than SPLASH_SPEED throws a few cells up.
	var knock_at := PackedVector2Array()
	var knock_sizes := PackedVector2Array()
	var knock_vels := PackedVector2Array()
	var knock_max := PackedInt32Array()
	var splash_at := PackedVector2Array()
	var splash_sizes := PackedVector2Array()
	var splash_vels := PackedVector2Array()
	var splash_max := PackedInt32Array()
	for i: int in count:
		if powders[i] > 0:
			knock_at.append(pos[i])
			knock_sizes.append(sizes[i])
			knock_vels.append(Vector2(KNOCK_SPEED, -KNOCK_SPEED))
			knock_max.append(int(sizes[i].x * sizes[i].y))
		var speed := to_world.basis_xform(_characters[i].linear_velocity).length()
		if liquids[i] > 0 and speed > SPLASH_SPEED:
			splash_at.append(pos[i])
			splash_sizes.append(sizes[i])
			splash_vels.append(Vector2(signf(cur[i].x - prev[i].x) * 1.5, -3.0))
			splash_max.append(mini(3, int(speed / SPLASH_SPEED)))
	if not knock_at.is_empty():
		_world.displace_cells(knock_at, knock_sizes, PixelWorld.RAY_POWDER, knock_vels, knock_max, true, 0.5)
	if not splash_at.is_empty():
		_world.displace_cells(splash_at, splash_sizes, PixelWorld.RAY_LIQUID, splash_vels, splash_max, false, -0.5)

	# 7. Standing on a crate or another character is floor too, from the Box2D contacts.
	var contacts: PackedInt32Array = _world.body_contacts(_characters)

	# 8. Write back: the corrected position, the velocity components the cells stopped, and
	#    what was found, ready for the queries game code uses.
	for i: int in count:
		var character := _characters[i]
		# Exact: a sub-cell correction must still land, or the box keeps a hair of itself in the
		# floor row and the unstick below lifts it a whole cell next tick.
		if pos[i].x != cur[i].x or pos[i].y != cur[i].y:
			character.teleport(to_global * pos[i])
		var body_floor := i < contacts.size() and contacts[i] & FLOOR != 0
		var floor_ := sides[i] & FLOOR != 0 or body_floor
		var ceiling := sides[i] & CEILING != 0
		var v := to_world.basis_xform(character.linear_velocity)
		if stopped[i] & PixelWorld.STOPPED_X != 0:
			v.x = 0.0
		if (floor_ and v.y > 0.0) or (ceiling and v.y < 0.0):
			v.y = 0.0
		character.linear_velocity = to_global.basis_xform(v)
		character._wall = 0
		if stopped[i] & PixelWorld.STOPPED_X != 0:
			character._wall = 1 if cur[i].x > prev[i].x else (-1 if cur[i].x < prev[i].x else 0)
		character._floor = floor_
		character._ceiling = ceiling
		character._was_on_floor = floor_
		var cells: float = sizes[i].x * sizes[i].y
		character._submerged = float(liquids[i]) / cells if cells > 0.0 else 0.0
		character._liquid = StringName(materials[i])
	last_tick_usec = Time.get_ticks_usec() - start
