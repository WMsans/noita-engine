extends Node2D

# PixelWorld benchmark: paints a fixed layout, pans the camera across the arena and back,
# ticks the simulator once per frame and writes frame/sim/upload timings as JSON.
#
#   godot --path . res://bench/bench.tscn -- --frames=1800 --out=bench/results/bench.json
#
# Needs a real window (not --headless): the frame time is what the frame budget is about.
# The scene sets `simulating = false`; one `step(1)` per frame keeps the tick count equal to
# the frame count instead of following the physics clock.

const DEFAULT_FRAMES := 1800
const WARMUP_FRAMES := 120  # discarded: pipeline warm-up and the first chunk streams
const SIM_BUDGET_MS := 6.0
const CHUNK := 64  # core CHUNK (cells per chunk side)

# Fixed layout in cells, arena x [-512, 511], y [-256, 255]. Values are the fixture bounds;
# the paint below fills them. Kept in one table because streaming regenerates unloaded
# chunks as air, so a fixture has to be painted again whenever its chunks stream back.
const FIXTURES := {
	&"lake": Rect2i(-460, 45, 300, 210),
	&"lava": Rect2i(-160, 175, 100, 80),
	&"sand": Rect2i(-36, 209, 172, 46),
	&"wood": Rect2i(180, 195, 40, 60),
	&"acid": Rect2i(270, 160, 160, 95),
}
const SAND_PILES: Array[Vector2i] = [Vector2i(-10, 235), Vector2i(50, 235), Vector2i(110, 235)]
const PILE_RADIUS := 26
const FAUCET_TOP := -230  # feeders re-painted every frame for the whole run
const FAUCET_SIZE := Vector2i(8, 3)
const PAN_LEFT := -412.0
const PAN_RIGHT := 411.0

@onready var world: PixelWorld = $PixelWorld
@onready var camera: Camera2D = $Camera2D

var total_frames := DEFAULT_FRAMES
var out_path := "user://bench.json"

var frame := 0
var last_usec := 0
var painted := {}

var frame_samples: Array[float] = []
var sim_samples: Array[float] = []
var upload_samples: Array[float] = []
var chunk_samples: Array[float] = []
var cell_samples: Array[float] = []


func _ready() -> void:
	_parse_args()
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	last_usec = Time.get_ticks_usec()


func _process(_delta: float) -> void:
	_pan_camera()
	_paint_streamed_fixtures()
	_feed_faucets()
	world.step(1)

	var stats: Dictionary = world.get_stats()
	var now := Time.get_ticks_usec()
	if frame >= WARMUP_FRAMES:
		frame_samples.append((now - last_usec) / 1000.0)
		sim_samples.append(float(stats.sim_ms))
		upload_samples.append(float(stats.upload_ms))
		chunk_samples.append(float(stats.chunks_simulated))
		cell_samples.append(float(stats.cells_updated))
	last_usec = now

	frame += 1
	if frame >= total_frames:
		_finish()


func _parse_args() -> void:
	for arg: String in OS.get_cmdline_user_args():
		if arg.begins_with("--frames="):
			total_frames = maxi(1, arg.trim_prefix("--frames=").to_int())
		elif arg.begins_with("--out="):
			out_path = arg.trim_prefix("--out=")


# Triangle wave across the arena: left to right over the first half of the run, back over the
# second half, with a small y drift so chunks above and below the target stream in too.
func _pan_camera() -> void:
	var progress := float(frame) / float(total_frames)
	var travel := fposmod(progress * 2.0, 2.0)
	if travel > 1.0:
		travel = 2.0 - travel
	camera.position = Vector2(lerpf(PAN_LEFT, PAN_RIGHT, travel), 20.0 * sin(progress * TAU))


func _paint_streamed_fixtures() -> void:
	for which: StringName in FIXTURES:
		var bounds: Rect2i = FIXTURES[which]
		if painted.get(which, false):
			# One probe per fixture per frame: when its first chunk goes away, its cells did too.
			if world.is_chunk_loaded(_chunk_of(bounds.position)):
				continue
			painted[which] = false
		if _rect_loaded(bounds):
			_paint_fixture(which)
			painted[which] = true


func _paint_fixture(which: StringName) -> void:
	match which:
		&"lake":  # 300-wide water lake with a 60-thick oil layer on top
			world.fill_rect(Rect2i(-460, 105, 300, 150), &"water")
			world.fill_rect(Rect2i(-460, 45, 300, 60), &"oil")
		&"lava":  # lava pool sharing a wall with the lake: steam and rock while it lasts
			world.fill_rect(Rect2i(-160, 175, 100, 80), &"lava")
		&"sand":
			for pile: Vector2i in SAND_PILES:
				world.fill_circle(pile, PILE_RADIUS, &"sand")
		&"wood":  # 40x60 lattice, ignited at its base
			world.fill_rect(Rect2i(180, 195, 6, 60), &"wood")
			world.fill_rect(Rect2i(214, 195, 6, 60), &"wood")
			for row in 5:
				world.fill_rect(Rect2i(180, 195 + row * 12, 40, 6), &"wood")
			world.fill_rect(Rect2i(186, 243, 28, 6), &"fire")
		&"acid":  # acid pocket resting on a rock slab
			world.fill_rect(Rect2i(270, 200, 160, 55), &"rock")
			world.fill_rect(Rect2i(300, 160, 100, 40), &"acid")


func _feed_faucets() -> void:
	for pile: Vector2i in SAND_PILES:
		world.fill_rect(Rect2i(pile.x - FAUCET_SIZE.x / 2, FAUCET_TOP, FAUCET_SIZE.x, FAUCET_SIZE.y), &"sand")


func _rect_loaded(rect: Rect2i) -> bool:
	var first := _chunk_of(rect.position)
	var last := _chunk_of(rect.position + rect.size - Vector2i.ONE)
	for cy in range(first.y, last.y + 1):
		for cx in range(first.x, last.x + 1):
			if not world.is_chunk_loaded(Vector2i(cx, cy)):
				return false
	return true


func _chunk_of(cell: Vector2i) -> Vector2i:
	return Vector2i(floori(float(cell.x) / CHUNK), floori(float(cell.y) / CHUNK))


func _finish() -> void:
	var sim_mean := _mean(sim_samples)
	var result := {
		"machine": OS.get_processor_name(),
		"os": OS.get_name(),
		"renderer": RenderingServer.get_video_adapter_name(),
		"worker_threads": world.worker_threads,  # -1 = auto, JobPool::default_workers()
		"frames": frame_samples.size(),
		"frame_ms_mean": _mean(frame_samples),
		"frame_ms_p99": _p99(frame_samples),
		"sim_ms_mean": sim_mean,
		"sim_ms_p99": _p99(sim_samples),
		"upload_ms_mean": _mean(upload_samples),
		"upload_ms_p99": _p99(upload_samples),
		"chunks_simulated_mean": _mean(chunk_samples),
		"cells_updated_mean": _mean(cell_samples),
		"budget_pass": sim_mean <= SIM_BUDGET_MS,
	}
	var written := _write_json(result)
	print("bench: %d frames (+%d warm-up) | sim %.2f ms mean / %.2f p99 | frame %.2f / %.2f ms | upload %.2f | chunks %.1f | cells %.0f | budget %s | %s" % [
		result.frames, WARMUP_FRAMES, result.sim_ms_mean, result.sim_ms_p99,
		result.frame_ms_mean, result.frame_ms_p99, result.upload_ms_mean,
		result.chunks_simulated_mean, result.cells_updated_mean,
		"pass" if result.budget_pass else "FAIL", out_path if written else "JSON NOT WRITTEN"])
	get_tree().quit(0 if written else 1)


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
