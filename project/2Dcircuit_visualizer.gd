class_name CircuitVisualizer
extends Node2D

@export var scale_factor: float = 2.0

var parser: SchParser


func _ready():
	parser = SchParser.new()


func load_schematic(path: String) -> bool:
	if not parser.parse_file(path):
		push_error("Failed to parse: " + path)
		return false

	_draw_circuit()
	return true


func _draw_circuit():
	for child in get_children():
		child.queue_free()

	for wire in parser.wires:
		_draw_wire(wire)

	for comp in parser.components:
		_draw_component(comp)


# ---------- Wires ----------

func _draw_wire(wire: Dictionary):
	var p1 = Vector2(wire.x1, -wire.y1) * scale_factor
	var p2 = Vector2(wire.x2, -wire.y2) * scale_factor

	var line = Line2D.new()
	line.width = 2.0
	line.default_color = Color.WHITE
	line.add_point(p1)
	line.add_point(p2)
	add_child(line)

	# Connection dots at endpoints
	_draw_connection_dot(p1)
	_draw_connection_dot(p2)

	# Wire label at midpoint
	if wire.label != "":
		var label = Label.new()
		label.text = wire.label
		label.position = (p1 + p2) / 2 + Vector2(5, -15)
		label.add_theme_font_size_override("font_size", 10)
		label.add_theme_color_override("font_color", Color.YELLOW)
		add_child(label)


func _draw_connection_dot(pos: Vector2):
	var dot = ColorRect.new()
	dot.size = Vector2(6, 6)
	dot.position = pos - dot.size / 2
	dot.color = Color.WHITE
	add_child(dot)


# ---------- Components ----------

func _draw_component(comp: Dictionary):
	var pos = Vector2(comp.x, -comp.y) * scale_factor
	var type = parser.get_component_type(comp.symbol)

	match type:
		"pmos":
			_draw_transistor(pos, comp, true)
		"nmos":
			_draw_transistor(pos, comp, false)
		"input_pin":
			_draw_input_pin(pos, comp)
		"output_pin":
			_draw_output_pin(pos, comp)
		"label":
			_draw_label_pin(pos, comp)
		"resistor":
			_draw_resistor(pos, comp)
		"capacitor":
			_draw_capacitor(pos, comp)
		_:
			_draw_generic(pos, comp, type)


## Creates a Node2D wrapper with position, rotation, and mirror applied.
func _make_symbol(pos: Vector2, comp: Dictionary) -> Node2D:
	var node = Node2D.new()
	node.position = pos
	var rot: int = comp.get("rotation", 0)
	var mirror: int = comp.get("mirror", 0)
	node.rotation = deg_to_rad(-rot * 90.0)
	if mirror:
		node.scale.x = -1
	add_child(node)
	return node


## Adds a label to self (not the rotated node) so text stays upright.
func _add_label(pos: Vector2, text: String, color: Color, offset: Vector2 = Vector2(15, -10)):
	var label = Label.new()
	label.text = text
	label.position = pos + offset
	label.add_theme_font_size_override("font_size", 10)
	label.add_theme_color_override("font_color", color)
	add_child(label)


func _draw_transistor(pos: Vector2, comp: Dictionary, is_pmos: bool):
	var color = Color.MAGENTA if is_pmos else Color.CYAN
	var node = _make_symbol(pos, comp)

	# Channel (vertical line on right, thicker)
	var channel = Line2D.new()
	channel.width = 3.0
	channel.default_color = color
	channel.add_point(Vector2(0, -30))
	channel.add_point(Vector2(0, 30))
	node.add_child(channel)

	# Gate bar (vertical line on left, parallel to channel)
	var gate = Line2D.new()
	gate.width = 2.0
	gate.default_color = color
	gate.add_point(Vector2(-10, -20))
	gate.add_point(Vector2(-10, 20))
	node.add_child(gate)

	# Gate connection (horizontal line to gate bar)
	var gate_conn = Line2D.new()
	gate_conn.width = 2.0
	gate_conn.default_color = color
	gate_conn.add_point(Vector2(-30, 0))
	gate_conn.add_point(Vector2(-10, 0))
	node.add_child(gate_conn)

	# Drain connection (horizontal line from channel top)
	var drain = Line2D.new()
	drain.width = 2.0
	drain.default_color = color
	drain.add_point(Vector2(0, -30))
	drain.add_point(Vector2(20, -30))
	node.add_child(drain)

	# Source connection (horizontal line from channel bottom)
	var source = Line2D.new()
	source.width = 2.0
	source.default_color = color
	source.add_point(Vector2(0, 30))
	source.add_point(Vector2(20, 30))
	node.add_child(source)

	# PMOS bubble (circle on gate)
	if is_pmos:
		var bubble = Line2D.new()
		bubble.width = 2.0
		bubble.default_color = color
		for i in range(13):
			var angle = i * PI * 2 / 12
			bubble.add_point(Vector2(-15, 0) + Vector2(cos(angle), sin(angle)) * 5)
		node.add_child(bubble)

	# Label — added to self so it stays upright
	_add_label(pos, comp.name, color, Vector2(25, -15))


func _draw_input_pin(pos: Vector2, comp: Dictionary):
	var color = Color.GREEN
	var node = _make_symbol(pos, comp)

	# Horizontal tail
	var tail = Line2D.new()
	tail.width = 2.0
	tail.default_color = color
	tail.add_point(Vector2(-30, 0))
	tail.add_point(Vector2(0, 0))
	node.add_child(tail)

	# Arrowhead ( > shape )
	var arrow_top = Line2D.new()
	arrow_top.width = 2.0
	arrow_top.default_color = color
	arrow_top.add_point(Vector2(0, -15))
	arrow_top.add_point(Vector2(25, 0))
	node.add_child(arrow_top)

	var arrow_bot = Line2D.new()
	arrow_bot.width = 2.0
	arrow_bot.default_color = color
	arrow_bot.add_point(Vector2(0, 15))
	arrow_bot.add_point(Vector2(25, 0))
	node.add_child(arrow_bot)

	_add_label(pos, "%s [%s]" % [comp.name, comp.label], color, Vector2(30, -15))


func _draw_output_pin(pos: Vector2, comp: Dictionary):
	var color = Color.RED
	var node = _make_symbol(pos, comp)

	# Circle indicator
	var circle = Line2D.new()
	circle.width = 2.0
	circle.default_color = color
	for i in range(13):
		var angle = i * PI * 2 / 12
		circle.add_point(Vector2(cos(angle), sin(angle)) * 12)
	node.add_child(circle)

	# Horizontal lead out
	var lead = Line2D.new()
	lead.width = 2.0
	lead.default_color = color
	lead.add_point(Vector2(12, 0))
	lead.add_point(Vector2(40, 0))
	node.add_child(lead)

	_add_label(pos, "%s [%s]" % [comp.name, comp.label], color, Vector2(15, -15))


func _draw_label_pin(pos: Vector2, comp: Dictionary):
	var color = Color.YELLOW
	var node = _make_symbol(pos, comp)

	# Small cross marker (+)
	var h_line = Line2D.new()
	h_line.width = 2.0
	h_line.default_color = color
	h_line.add_point(Vector2(-10, 0))
	h_line.add_point(Vector2(10, 0))
	node.add_child(h_line)

	var v_line = Line2D.new()
	v_line.width = 2.0
	v_line.default_color = color
	v_line.add_point(Vector2(0, -10))
	v_line.add_point(Vector2(0, 10))
	node.add_child(v_line)

	_add_label(pos, "%s [%s]" % [comp.name, comp.label], color, Vector2(12, -15))


func _draw_resistor(pos: Vector2, comp: Dictionary):
	var color = Color.ORANGE
	var node = _make_symbol(pos, comp)

	# Zigzag body with leads
	var zigzag = Line2D.new()
	zigzag.width = 2.0
	zigzag.default_color = color
	var pts = [
		Vector2(0, -30),
		Vector2(15, -20),
		Vector2(-15, -10),
		Vector2(15, 0),
		Vector2(-15, 10),
		Vector2(15, 20),
		Vector2(0, 30),
	]
	for pt in pts:
		zigzag.add_point(pt)
	node.add_child(zigzag)

	_add_label(pos, comp.name, color, Vector2(20, -15))


func _draw_capacitor(pos: Vector2, comp: Dictionary):
	var color = Color(0.4, 0.6, 1.0)
	var node = _make_symbol(pos, comp)

	# Top lead
	var top_lead = Line2D.new()
	top_lead.width = 2.0
	top_lead.default_color = color
	top_lead.add_point(Vector2(0, -30))
	top_lead.add_point(Vector2(0, -8))
	node.add_child(top_lead)

	# Top plate
	var top_plate = Line2D.new()
	top_plate.width = 3.0
	top_plate.default_color = color
	top_plate.add_point(Vector2(-20, -8))
	top_plate.add_point(Vector2(20, -8))
	node.add_child(top_plate)

	# Bottom plate
	var bot_plate = Line2D.new()
	bot_plate.width = 3.0
	bot_plate.default_color = color
	bot_plate.add_point(Vector2(-20, 8))
	bot_plate.add_point(Vector2(20, 8))
	node.add_child(bot_plate)

	# Bottom lead
	var bot_lead = Line2D.new()
	bot_lead.width = 2.0
	bot_lead.default_color = color
	bot_lead.add_point(Vector2(0, 8))
	bot_lead.add_point(Vector2(0, 30))
	node.add_child(bot_lead)

	_add_label(pos, comp.name, color, Vector2(25, -15))


func _draw_generic(pos: Vector2, comp: Dictionary, type: String):
	var node = _make_symbol(pos, comp)

	var dot = ColorRect.new()
	dot.size = Vector2(8, 8)
	dot.position = -dot.size / 2
	dot.color = _get_color(type)
	node.add_child(dot)

	_add_label(pos, "%s (%d, %d)" % [type, comp.x, comp.y], _get_color(type), Vector2(10, -12))


func _get_color(type: String) -> Color:
	match type:
		"pmos": return Color.MAGENTA
		"nmos": return Color.CYAN
		"input_pin": return Color.GREEN
		"output_pin": return Color.RED
		"label": return Color.YELLOW
		"resistor": return Color.ORANGE
		"capacitor": return Color(0.4, 0.6, 1.0)
		_: return Color.GRAY
