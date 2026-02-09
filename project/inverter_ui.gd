extends CanvasLayer

@onready var _sim = get_parent()
@onready var _play_pause: Button = $Panel/Margin/VBox/PlayRow/PlayPauseButton
@onready var _vdd_slider: HSlider = $Panel/Margin/VBox/VddRow/VddSlider
@onready var _vdd_value: Label = $Panel/Margin/VBox/VddRow/VddValue
@onready var _input_toggle: CheckButton = $Panel/Margin/VBox/InputRow/InputToggle


func _ready() -> void:
	if !_sim.has_method("set_vdd") or !_sim.has_method("set_input_high"):
		push_error("UI is not attached to circuit_simulator backend node")
		return

	_play_pause.pressed.connect(_on_play_pause_pressed)
	_vdd_slider.value_changed.connect(_on_vdd_changed)
	_input_toggle.toggled.connect(_on_input_toggled)

	if _sim.has_signal("playback_state_changed"):
		_sim.playback_state_changed.connect(_on_playback_state_changed)

	_refresh_vdd(_vdd_slider.value)
	_on_playback_state_changed(true)


func _on_play_pause_pressed() -> void:
	if _play_pause.text == "Pause":
		_sim.pause_animation()
	else:
		_sim.play_animation()


func _on_vdd_changed(value: float) -> void:
	_refresh_vdd(value)
	_sim.set_vdd(value)


func _on_input_toggled(pressed: bool) -> void:
	_sim.set_input_high(pressed)


func _on_playback_state_changed(is_playing: bool) -> void:
	_play_pause.text = "Pause" if is_playing else "Play"


func _refresh_vdd(value: float) -> void:
	_vdd_value.text = "%.2f V" % value
