extends Node2D

# Streaming benchmark: flies the camera along a looping polyline over the generated world,
# paints the workload at offsets from the camera, ticks the simulator once per frame and
# writes frame/sim/stream/generation timings as JSON.
#
#   godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/world.json
#
# Needs a real window (not --headless): the frame time is what the frame budget is about.
# The scene sets `simulating = false`; one `step(1)` per frame keeps the tick count equal to
# the frame count instead of following the physics clock.

const DEFAULT_FRAMES := 1800
const WARMUP_FRAMES := 120  # discarded: pipeline warm-up and the first chunks stream in
const SIM_BUDGET_MS := 6.0
const STREAM_BUDGET_MS := 1.0
const STREAM_P99_BUDGET_MS := 3.0
const FRAME_P99_BUDGET_MS := 16.6
const PARTICLE_BUDGET_MS := 1.0
# A bench that makes no particles must not pass: at least this many material particles in
# flight on average.
const PARTICLES_BUDGET := 200.0
const BODY_BUDGET_MS := 2.0  # Box2D step + terrain shapes + stamping
const BODIES_BUDGET := 40.0  # bodies alive on average, so a bench without bodies cannot pass
const CREATURE_BUDGET_MS := 1.0
const CREATURES_BUDGET := 30.0  # creatures alive on average
# Lighting: the light-input build (grid walk + light copy) per frame, the GPU light + bloom
# pass, and the spots that prove the pass ran on real lights.
const LIGHT_CPU_BUDGET_MS := 1.0
const LIGHT_GPU_BUDGET_MS := 3.0
const LIGHT_SPOTS_BUDGET := 1.0
# 50% of Plan 1's M1 figure (108383.9), the plan's floor for this bench: the generated world
# must keep at least half an arena frame of work moving, so a gutted streaming demo cannot pass
# by doing less work. The committed M1 run reports 3x this (325127).
const CELLS_BUDGET := 54192.0

# The camera is frame-locked, not time-based: 10 cells per frame is 600 cells/s at 60 fps and
# stricter at higher frame rates. Loops: A -> B along the surface, C the dive through every
# band, D the cavern run, E back up, then A again over discarded ground.
const CELLS_PER_FRAME := 10.0
const WAYPOINTS: Array[Vector2] = [
	Vector2(0, -40),  # A
	Vector2(2400, -40),  # B
	Vector2(2400, 2000),  # C
	Vector2(-600, 1400),  # D
	Vector2(-600, -40),  # E
]

# Workload offsets in cells from the camera. Painted every frame, so the workload travels with
# the camera and streaming never lets the fixtures fall out of the zone it measures.
const SAND_FAUCET_SIZE := Vector2i(8, 3)
const SAND_FAUCET_OFFSETS: Array[Vector2i] = [Vector2i(-150, -110), Vector2i(150, -110)]
const WATER_FAUCET_SIZE := Vector2i(6, 3)
const WATER_FAUCET_OFFSET := Vector2i(0, -120)
const OIL_PERIOD := 30  # frames between the oil + fire drops
const OIL_OFFSET := Vector2i(220, 40)
const OIL_RADIUS := 10
const FIRE_RADIUS := 3
# Every EXPLODE_PERIOD frames: fill a sand disc and blow it up (starter ground is solid, so a
# blast in it would throw nothing), plus a burst of visual sparks at the same spot.
const EXPLODE_PERIOD := 20
const EXPLODE_OFFSET := Vector2i(-60, -40)
const EXPLODE_RADIUS := 24
const EXPLODE_POWER := 400.0  # cells per second at the centre
const SPARK_COUNT := 150
const SPARK_LIFETIME := 0.8
# Bodies: every BODY_PERIOD frames drop BODY_BATCH (crates, every third a boulder) in a row
# above the camera; any further than BODY_FREE_DISTANCE from the camera, or that left the
# world, is freed. At 10 cells/frame that keeps about 60 alive.
const BODY_PERIOD := 10
const BODY_BATCH := 10
const BODY_MAX := 60
const BODY_FREE_DISTANCE := 600.0
# Creatures: every CREATURE_PERIOD frames drop CREATURE_BATCH wanderers in a row above the
# camera; they walk, turn at walls and hop. Freed like the bodies.
const CREATURE_PERIOD := 10
const CREATURE_BATCH := 8
const CREATURE_MAX := 48
const WANDER_SPEED := 60.0
const HOP_PERIOD := 90
const HOP_SPEED := 180.0
# Lighting: a lava pool every LAVA_PERIOD frames at a camera offset, plus the torch that is a
# child of the camera in the scene. The lava lights its chunk's cells and the torch is a spot.
const LAVA_PERIOD := 60
const LAVA_SIZE := Vector2i(40, 6)
const LAVA_OFFSET := Vector2i(-200, 60)

@onready var world: PixelWorld = $PixelWorld
@onready var camera: Camera2D = $Camera2D

var total_frames := DEFAULT_FRAMES
var out_path := "user://bench_world.json"

var frame := 0
var last_usec := 0
var refused := false  # bad command line: quit without simulating or writing a JSON
var path_length := 0.0

var frame_samples: Array[float] = []
var sim_samples: Array[float] = []
var upload_samples: Array[float] = []
var chunk_samples: Array[float] = []
var cell_samples: Array[float] = []
var stream_samples: Array[float] = []
var gen_samples: Array[float] = []  # only frames that finished a chunk
var particle_samples: Array[float] = []
var particles_material_samples: Array[float] = []
var particles_visual_samples: Array[float] = []
var body_samples: Array[float] = []
var bodies_awake_samples: Array[float] = []
var physics_samples: Array[float] = []
var shape_samples: Array[float] = []
var stamp_samples: Array[float] = []
var shape_build_samples: Array[float] = []
var body_cost_samples: Array[float] = []
var creature_samples: Array[float] = []
var creature_ms_samples: Array[float] = []
var light_cpu_samples: Array[float] = []
var light_gpu_samples: Array[float] = []
var light_spots_samples: Array[float] = []
var creatures: Array[PixelCharacter] = []
var bodies: Array[PixelRigidBody] = []
var crate_texture: ImageTexture
var boulder_texture: ImageTexture
var view_missing_frames := 0


func _ready() -> void:
	_parse_args()
	if refused:
		return
	for i in WAYPOINTS.size():
		path_length += WAYPOINTS[i].distance_to(WAYPOINTS[(i + 1) % WAYPOINTS.size()])
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	crate_texture = _texture(12, Color(0.55, 0.35, 0.18), false)
	boulder_texture = _texture(14, Color(0.45, 0.45, 0.48), true)
	last_usec = Time.get_ticks_usec()


func _process(_delta: float) -> void:
	if refused:
		return
	var centre := _fly_camera()
	_paint_workload(centre)
	world.step(1)

	var stats: Dictionary = world.get_stats()
	var now := Time.get_ticks_usec()
	if frame >= WARMUP_FRAMES:
		frame_samples.append((now - last_usec) / 1000.0)
		sim_samples.append(float(stats.sim_ms))
		upload_samples.append(float(stats.upload_ms))
		chunk_samples.append(float(stats.chunks_simulated))
		cell_samples.append(float(stats.cells_updated))
		stream_samples.append(float(stats.stream_ms))
		particle_samples.append(float(stats.particle_ms))
		particles_material_samples.append(float(stats.particles_material))
		particles_visual_samples.append(float(stats.particles_visual))
		body_samples.append(float(stats.bodies))
		bodies_awake_samples.append(float(stats.bodies_awake))
		physics_samples.append(float(stats.physics_ms))
		shape_samples.append(float(stats.shape_ms))
		stamp_samples.append(float(stats.stamp_ms))
		shape_build_samples.append(float(stats.shape_builds))
		body_cost_samples.append(float(stats.physics_ms) + float(stats.shape_ms) + float(stats.stamp_ms))
		creature_samples.append(float(stats.creatures))
		creature_ms_samples.append(float(stats.creature_ms))
		light_cpu_samples.append(float(stats.light_cpu_ms))
		light_gpu_samples.append(float(stats.light_gpu_ms))
		light_spots_samples.append(float(stats.light_spots))
		if float(stats.gen_ms) > 0.0:
			gen_samples.append(float(stats.gen_ms))
		if int(stats.view_missing) > 0:
			view_missing_frames += 1
	last_usec = now

	frame += 1
	if frame >= total_frames:
		_finish()


func _parse_args() -> void:
	for arg: String in OS.get_cmdline_user_args():
		if arg.begins_with("--frames="):
			var requested := arg.trim_prefix("--frames=").to_int()
			if requested <= WARMUP_FRAMES:
				# Without this the run measures nothing and still reports budget_pass.
				_refuse("%s leaves no measured frames: the first %d frames are warm-up, so --frames must be > %d" % [arg, WARMUP_FRAMES, WARMUP_FRAMES])
				return
			total_frames = requested
		elif arg.begins_with("--out="):
			out_path = arg.trim_prefix("--out=")


# Walk the closed polyline at CELLS_PER_FRAME and return the camera cell.
func _fly_camera() -> Vector2i:
	var travel := fposmod(float(frame) * CELLS_PER_FRAME, path_length)
	var at := WAYPOINTS[0]
	for i in WAYPOINTS.size():
		var from: Vector2 = WAYPOINTS[i]
		var to: Vector2 = WAYPOINTS[(i + 1) % WAYPOINTS.size()]
		var length := from.distance_to(to)
		if travel <= length:
			at = from.lerp(to, travel / length)
			break
		travel -= length
	camera.position = at
	return Vector2i(floori(at.x), floori(at.y))


func _paint_workload(centre: Vector2i) -> void:
	for offset: Vector2i in SAND_FAUCET_OFFSETS:
		world.fill_rect(Rect2i(centre + offset, SAND_FAUCET_SIZE), &"sand")
	world.fill_rect(Rect2i(centre + WATER_FAUCET_OFFSET, WATER_FAUCET_SIZE), &"water")
	if frame % OIL_PERIOD == 0:
		var oil := centre + OIL_OFFSET
		world.fill_circle(oil, OIL_RADIUS, &"oil")
		world.fill_circle(oil, FIRE_RADIUS, &"fire")
	if frame % EXPLODE_PERIOD == 0:
		var blast := centre + EXPLODE_OFFSET
		world.fill_circle(blast, EXPLODE_RADIUS, &"sand")
		world.explode(blast, EXPLODE_RADIUS, EXPLODE_POWER)
		world.spawn_visual_particles(Color.YELLOW, Vector2(blast), Vector2(0, -200), SPARK_COUNT, PI, SPARK_LIFETIME)
	if frame % LAVA_PERIOD == 0:
		world.fill_rect(Rect2i(centre + LAVA_OFFSET, LAVA_SIZE), &"lava")
	_cycle_bodies(centre)
	_cycle_creatures(centre)


func _cycle_creatures(centre: Vector2i) -> void:
	for i in range(creatures.size() - 1, -1, -1):
		var creature := creatures[i]
		if creature.global_position.distance_to(Vector2(centre)) > BODY_FREE_DISTANCE or creature.has_meta(&"left"):
			creature.queue_free()
			creatures.remove_at(i)
			continue
		var dir: float = creature.get_meta(&"dir")
		if creature.get_wall_side() == int(signf(dir)):
			dir = -dir
			creature.set_meta(&"dir", dir)
		var v := creature.velocity
		v.x = dir * WANDER_SPEED
		if creature.is_on_floor() and (frame + i) % HOP_PERIOD == 0:
			v.y = -HOP_SPEED
		creature.velocity = v
	if frame % CREATURE_PERIOD != 0:
		return
	for i in CREATURE_BATCH:
		if creatures.size() >= CREATURE_MAX:
			return
		var creature := PixelCharacter.new()
		creature.position = Vector2(centre) + Vector2(-140 + 40 * i, -80)
		creature.set_meta(&"dir", 1.0 if i % 2 == 0 else -1.0)
		creature.left_world.connect(creature.set_meta.bind(&"left", true))
		add_child(creature)
		creatures.append(creature)


func _cycle_bodies(centre: Vector2i) -> void:
	for i in range(bodies.size() - 1, -1, -1):
		var body := bodies[i]
		if body.global_position.distance_to(Vector2(centre)) > BODY_FREE_DISTANCE or body.has_meta(&"left"):
			body.queue_free()
			bodies.remove_at(i)
	if frame % BODY_PERIOD != 0:
		return
	for i in BODY_BATCH:
		if bodies.size() >= BODY_MAX:
			return
		var body := PixelRigidBody.new()
		var boulder := i % 3 == 2
		body.texture = boulder_texture if boulder else crate_texture
		body.pixel_material = &"rock" if boulder else &"wood"
		body.shape = PixelRigidBody.SHAPE_CIRCLE if boulder else PixelRigidBody.SHAPE_HULL
		body.position = Vector2(centre) + Vector2(-135 + 30 * i, -100)
		body.left_world.connect(body.set_meta.bind(&"left", true))
		add_child(body)
		bodies.append(body)


func _texture(size: int, colour: Color, disc: bool) -> ImageTexture:
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	for y: int in size:
		for x: int in size:
			var d := Vector2(x + 0.5 - size / 2.0, y + 0.5 - size / 2.0).length()
			image.set_pixel(x, y, colour if not disc or d <= size / 2.0 else Color(0, 0, 0, 0))
	return ImageTexture.create_from_image(image)


func _finish() -> void:
	if frame_samples.is_empty():
		_refuse("no frame samples were collected after %d frames, so there is nothing to report" % frame)
		return
	var final_stats: Dictionary = world.get_stats()
	var sim_mean := _mean(sim_samples)
	var stream_mean := _mean(stream_samples)
	var stream_p99 := _p99(stream_samples)
	var frame_p99 := _p99(frame_samples)
	var cells_mean := _mean(cell_samples)
	var particle_mean := _mean(particle_samples)
	var particles_mean := _mean(particles_material_samples)
	var body_cost_mean := _mean(body_cost_samples)
	var bodies_mean := _mean(body_samples)
	var creatures_mean := _mean(creature_samples)
	var creature_ms_mean := _mean(creature_ms_samples)
	var light_cpu_mean := _mean(light_cpu_samples)
	var light_gpu_mean := _mean(light_gpu_samples)
	var light_spots_mean := _mean(light_spots_samples)
	# macOS/Metal stubs RenderingDevice timestamp queries (the driver zeroes the results), so a
	# driver that reports no GPU time at all is recorded as unsupported and only the "GPU time is
	# above zero" clause is skipped. The <= 3 ms budget, the CPU budget and the spot count are
	# kept as the proof that lighting ran.
	var light_gpu_supported := false
	for sample: float in light_gpu_samples:
		if sample > 0.0:
			light_gpu_supported = true
			break
	var result := {
		"machine": OS.get_processor_name(),
		"os": OS.get_name(),
		"renderer": RenderingServer.get_video_adapter_name(),
		"worker_threads": world.worker_threads,  # -1 = auto, JobPool::default_workers()
		"frames": frame_samples.size(),
		"frame_ms_mean": _mean(frame_samples),
		"frame_ms_p99": frame_p99,
		"sim_ms_mean": sim_mean,
		"sim_ms_p99": _p99(sim_samples),
		"upload_ms_mean": _mean(upload_samples),
		"upload_ms_p99": _p99(upload_samples),
		"chunks_simulated_mean": _mean(chunk_samples),
		"cells_updated_mean": cells_mean,
		"stream_ms_mean": stream_mean,
		"stream_ms_p99": stream_p99,
		"gen_ms_mean": _mean(gen_samples),
		"chunks_generated": int(final_stats.chunks_generated),
		"view_missing_frames": view_missing_frames,
		"particle_ms_mean": particle_mean,
		"particle_ms_p99": _p99(particle_samples),
		"particles_material_mean": particles_mean,
		"particles_visual_mean": _mean(particles_visual_samples),
		"particles_dropped": int(final_stats.particles_dropped),
		"bodies_mean": bodies_mean,
		"bodies_awake_mean": _mean(bodies_awake_samples),
		"physics_ms_mean": _mean(physics_samples),
		"physics_ms_p99": _p99(physics_samples),
		"shape_ms_mean": _mean(shape_samples),
		"shape_ms_p99": _p99(shape_samples),
		"stamp_ms_mean": _mean(stamp_samples),
		"stamp_ms_p99": _p99(stamp_samples),
		"shape_builds_mean": _mean(shape_build_samples),
		"body_ms_mean": body_cost_mean,
		"body_ms_p99": _p99(body_cost_samples),
		"creatures_mean": creatures_mean,
		"creature_ms_mean": creature_ms_mean,
		"creature_ms_p99": _p99(creature_ms_samples),
		"light_cpu_ms_mean": light_cpu_mean,
		"light_cpu_ms_p99": _p99(light_cpu_samples),
		"light_gpu_ms_mean": light_gpu_mean,
		"light_gpu_ms_p99": _p99(light_gpu_samples),
		"light_gpu_ms_supported": light_gpu_supported,
		"light_spots_mean": light_spots_mean,
		"budget_pass": sim_mean <= SIM_BUDGET_MS
			and stream_mean <= STREAM_BUDGET_MS
			and stream_p99 <= STREAM_P99_BUDGET_MS
			and frame_p99 <= FRAME_P99_BUDGET_MS
			and view_missing_frames == 0
			and cells_mean >= CELLS_BUDGET
			and particle_mean <= PARTICLE_BUDGET_MS
			and particles_mean >= PARTICLES_BUDGET
			and body_cost_mean <= BODY_BUDGET_MS
			and bodies_mean >= BODIES_BUDGET
			and creature_ms_mean <= CREATURE_BUDGET_MS
			and creatures_mean >= CREATURES_BUDGET
			and light_cpu_mean <= LIGHT_CPU_BUDGET_MS
			and light_gpu_mean <= LIGHT_GPU_BUDGET_MS
			and (not light_gpu_supported or light_gpu_mean > 0.0)
			and light_spots_mean >= LIGHT_SPOTS_BUDGET,
	}
	var written := _write_json(result)
	print("bench world: %d frames (+%d warm-up) | sim %.2f ms mean / %.2f p99 | frame %.2f / %.2f ms | stream %.2f / %.2f ms | gen %.2f ms | chunks %d | cells %.0f | view-missing %d frames | particles %.0f / visual %.0f, %.3f ms mean / %.3f p99, dropped %d | bodies %.0f (awake %.0f), physics %.2f + shapes %.2f + stamp %.2f = %.2f ms mean / %.2f p99, %.1f builds | creatures %.0f, %.3f ms mean / %.3f p99 | light %.2f ms cpu mean / %.2f p99 | light %.3f ms gpu mean / %.3f p99 (timestamps %s), spots %.1f | budget %s | %s" % [
		result.frames, WARMUP_FRAMES, result.sim_ms_mean, result.sim_ms_p99,
		result.frame_ms_mean, result.frame_ms_p99, result.stream_ms_mean,
		result.stream_ms_p99, result.gen_ms_mean, result.chunks_generated,
		result.cells_updated_mean, result.view_missing_frames,
		result.particles_material_mean, result.particles_visual_mean, result.particle_ms_mean,
		result.particle_ms_p99, result.particles_dropped,
		result.bodies_mean, result.bodies_awake_mean, result.physics_ms_mean, result.shape_ms_mean,
		result.stamp_ms_mean, result.body_ms_mean, result.body_ms_p99, result.shape_builds_mean,
		result.creatures_mean, result.creature_ms_mean, result.creature_ms_p99,
		result.light_cpu_ms_mean, result.light_cpu_ms_p99,
		result.light_gpu_ms_mean, result.light_gpu_ms_p99,
		"reported" if result.light_gpu_ms_supported else "stubbed", result.light_spots_mean,
		"pass" if result.budget_pass else "FAIL", out_path if written else "JSON NOT WRITTEN"])
	get_tree().quit(0 if written else 1)


func _refuse(reason: String) -> void:
	refused = true
	printerr("bench: refusing to run: %s; no JSON written" % reason)
	get_tree().quit(1)


func _mean(values: Array[float]) -> float:
	if values.is_empty():
		return 0.0
	var total := 0.0
	for value in values:
		total += value
	return total / values.size()


func _p99(values: Array[float]) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[mini(sorted.size() - 1, ceili(0.99 * sorted.size()) - 1)]


func _write_json(data: Dictionary) -> bool:
	var path := _absolute_path(out_path)
	var dir := path.get_base_dir()
	if dir != "" and not DirAccess.dir_exists_absolute(dir):
		DirAccess.make_dir_recursive_absolute(dir)
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		printerr("bench: cannot write %s (%s)" % [path, error_string(FileAccess.get_open_error())])
		return false
	file.store_string(JSON.stringify(data, "\t") + "\n")
	file.close()
	return true


# Relative --out paths belong to the current directory, not to the project directory.
func _absolute_path(path: String) -> String:
	if path.begins_with("res://") or path.begins_with("user://") or path.is_absolute_path():
		return path
	return DirAccess.open(".").get_current_dir().path_join(path)
