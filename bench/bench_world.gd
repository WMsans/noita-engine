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
var view_missing_frames := 0


func _ready() -> void:
	_parse_args()
	if refused:
		return
	for i in WAYPOINTS.size():
		path_length += WAYPOINTS[i].distance_to(WAYPOINTS[(i + 1) % WAYPOINTS.size()])
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
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
		"budget_pass": sim_mean <= SIM_BUDGET_MS
			and stream_mean <= STREAM_BUDGET_MS
			and stream_p99 <= STREAM_P99_BUDGET_MS
			and frame_p99 <= FRAME_P99_BUDGET_MS
			and view_missing_frames == 0
			and cells_mean >= CELLS_BUDGET,
	}
	var written := _write_json(result)
	print("bench world: %d frames (+%d warm-up) | sim %.2f ms mean / %.2f p99 | frame %.2f / %.2f ms | stream %.2f / %.2f ms | gen %.2f ms | chunks %d | cells %.0f | view-missing %d frames | budget %s | %s" % [
		result.frames, WARMUP_FRAMES, result.sim_ms_mean, result.sim_ms_p99,
		result.frame_ms_mean, result.frame_ms_p99, result.stream_ms_mean,
		result.stream_ms_p99, result.gen_ms_mean, result.chunks_generated,
		result.cells_updated_mean, result.view_missing_frames,
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
