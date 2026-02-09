extends Control

@export var max_points := 1200
@export var vin_color := Color(0.15, 0.8, 0.25, 1.0)
@export var vout_color := Color(0.95, 0.35, 0.2, 1.0)
@export var grid_color := Color(0.35, 0.35, 0.35, 0.6)
@export var axis_color := Color(0.7, 0.7, 0.7, 0.8)

var _times: Array[float] = []
var _vin: Array[float] = []
var _vout: Array[float] = []
var _tmin := 0.0
var _tmax := 1.0
var _vmin := 0.0
var _vmax := 2.0


func clear_plot() -> void:
	_times.clear()
	_vin.clear()
	_vout.clear()
	_tmin = 0.0
	_tmax = 1.0
	queue_redraw()


func add_sample(time_s: float, vin: float, vout: float) -> void:
	_times.append(time_s)
	_vin.append(vin)
	_vout.append(vout)

	if _times.size() == 1:
		_tmin = time_s
		_tmax = time_s + 1e-9
	else:
		_tmin = _times[0]
		_tmax = _times[_times.size() - 1]
		if is_equal_approx(_tmin, _tmax):
			_tmax = _tmin + 1e-9

	if _times.size() > max_points:
		_times.remove_at(0)
		_vin.remove_at(0)
		_vout.remove_at(0)
		_tmin = _times[0]
		_tmax = _times[_times.size() - 1]

	queue_redraw()


func _draw() -> void:
	var rect := Rect2(Vector2.ZERO, size)
	draw_rect(rect, Color(0.08, 0.08, 0.1, 0.95), true)

	_draw_grid()

	if _times.size() < 2:
		return

	_draw_trace(_vin, vin_color, 2.0)
	_draw_trace(_vout, vout_color, 2.0)


func _draw_grid() -> void:
	var w := size.x
	var h := size.y
	for i in range(1, 5):
		var y := h * float(i) / 5.0
		draw_line(Vector2(0, y), Vector2(w, y), grid_color, 1.0)
	for i in range(1, 8):
		var x := w * float(i) / 8.0
		draw_line(Vector2(x, 0), Vector2(x, h), grid_color, 1.0)

	var zero_y := _voltage_to_y(0.0)
	draw_line(Vector2(0, zero_y), Vector2(w, zero_y), axis_color, 1.0)


func _draw_trace(values: Array[float], color: Color, width: float) -> void:
	var points := PackedVector2Array()
	points.resize(_times.size())

	for i in _times.size():
		var x := _time_to_x(_times[i])
		var y := _voltage_to_y(values[i])
		points[i] = Vector2(x, y)

	draw_polyline(points, color, width, true)


func _time_to_x(t: float) -> float:
	return ((t - _tmin) / (_tmax - _tmin)) * size.x


func _voltage_to_y(v: float) -> float:
	var clamped := clampf(v, _vmin, _vmax)
	var n := (clamped - _vmin) / (_vmax - _vmin)
	return size.y * (1.0 - n)
