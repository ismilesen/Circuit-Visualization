extends CircuitSimulator

signal waveform_updated(time_s: float, vin: float, vout: float)
signal playback_state_changed(is_playing: bool)

@export var playback_speed := 1.0

var _params := {
	"vdd": 1.8,
	"vin_level": 0.0,
	"tstep": 0.1e-9,
	"tstop": 200e-9,
	"c_load": 20e-15,
	"wn": 1e-6,
	"wp": 2e-6,
	"l": 0.18e-6
}

var _times: Array[float] = []
var _vin: Array[float] = []
var _vout: Array[float] = []
var _playback_time := 0.0
var _playback_index := 0
var _is_playing := false
var _sim_time_span := 0.0


func _ready() -> void:
	simulation_data_ready.connect(_on_simulation_data_ready)
	simulation_started.connect(_on_simulation_started)
	simulation_finished.connect(_on_simulation_finished)

	if !initialize_ngspice():
		push_error("ngspice init failed")
		return

	start_inverter_demo()


func _process(delta: float) -> void:
	if !_is_playing or _times.is_empty():
		return

	var sim_seconds_per_real_second := maxf(_sim_time_span / 2.0, 1e-9) * playback_speed
	_playback_time += delta * sim_seconds_per_real_second
	while _playback_index + 1 < _times.size() and _times[_playback_index + 1] <= _playback_time:
		_playback_index += 1

	if _playback_index < _times.size():
		waveform_updated.emit(_times[_playback_index], _vin[_playback_index], _vout[_playback_index])


func start_inverter_demo(custom_params: Dictionary = {}) -> void:
	for key in custom_params.keys():
		_params[String(key)] = float(custom_params[key])

	_clear_waveform()

	if !load_inverter_demo(_params):
		push_error("failed to load inverter netlist")
		return

	if !run_simulation():
		push_error("failed to start background simulation")
		return

	_is_playing = true
	playback_state_changed.emit(true)


func pause_animation() -> void:
	_is_playing = false
	pause_simulation()
	playback_state_changed.emit(false)


func play_animation() -> void:
	_is_playing = true
	resume_simulation()
	playback_state_changed.emit(true)


func update_parameter(name: String, value: float) -> bool:
	return update_parameters({name: value})


func update_parameters(values: Dictionary) -> bool:
	for key in values.keys():
		var lower_name := String(key).to_lower()
		var scalar := float(values[key])
		_params[lower_name] = scalar

	return _reload_and_run_with_params()


func set_vdd(voltage: float) -> bool:
	return update_parameter("VDD", voltage)


func set_input_high(enabled: bool) -> bool:
	var vin_level: float = float(_params["vdd"]) if enabled else 0.0
	return update_parameter("VIN_LEVEL", vin_level)


func _on_simulation_data_ready(data: Dictionary) -> void:
	if !data.has("time") or !data.has("v(in)") or !data.has("v(out)"):
		return

	var time_s := float(data["time"])
	var vin := float(data["v(in)"])
	var vout := float(data["v(out)"])

	_times.append(time_s)
	_vin.append(vin)
	_vout.append(vout)

	if _times.size() == 1:
		_playback_time = time_s
		_playback_index = 0
	else:
		_sim_time_span = _times[_times.size() - 1] - _times[0]


func _on_simulation_started() -> void:
	# Keeps UI controls in sync when ngspice starts running in background.
	playback_state_changed.emit(_is_playing)


func _on_simulation_finished() -> void:
	_ingest_current_vectors()


func _clear_waveform() -> void:
	_times.clear()
	_vin.clear()
	_vout.clear()
	_playback_time = 0.0
	_playback_index = 0
	_sim_time_span = 0.0


func _reload_and_run_with_params() -> bool:
	if is_running():
		stop_simulation()

	_clear_waveform()

	if !load_inverter_demo(_params):
		push_error("failed to reload inverter netlist with updated parameters")
		return false

	if !run_simulation():
		push_error("failed to restart simulation after parameter update")
		return false

	return true


func _ingest_current_vectors() -> void:
	var t: Array = get_time_vector()
	var vin: Array = get_voltage("in")
	var vout: Array = get_voltage("out")
	var n := mini(t.size(), mini(vin.size(), vout.size()))
	if n == 0:
		return

	_clear_waveform()
	for i in n:
		_times.append(float(t[i]))
		_vin.append(float(vin[i]))
		_vout.append(float(vout[i]))

	_playback_index = 0
	_playback_time = _times[0]
	_sim_time_span = _times[n - 1] - _times[0]

	# Emit first sample immediately so UI/plot confirms backend linkage.
	waveform_updated.emit(_times[0], _vin[0], _vout[0])
