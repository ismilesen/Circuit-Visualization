class_name CircuitVisualizer3D
extends Node3D

@export var scale_factor: float = 0.01

var parser: SchParser

## Cached SymbolDefinition objects keyed by symbol name.
var _sym_cache: Dictionary = {}

## Cached materials keyed by component type.
var _materials: Dictionary = {}

## Search paths for resolving symbol file names to res:// paths.
var _sym_search_paths: Array[String] = [
	"res://symbols/sym/",
	"res://symbols/sym/sky130_fd_pr/",
	"res://symbols/",
	"res://symbols/sky130_fd_pr/",
]


func _ready():
	parser = SchParser.new()
	_build_materials()


func load_schematic(path: String) -> bool:
	if not parser.parse_file(path):
		push_error("Failed to parse: " + path)
		return false

	_draw_circuit()
	return true


func _build_materials():
	_materials.clear()
	var defs = {
		"pmos": Color(1.0, 0.2, 0.8),
		"nmos": Color(0.2, 0.8, 1.0),
		"input_pin": Color(0.2, 1.0, 0.3),
		"ipin": Color(0.2, 1.0, 0.3),
		"output_pin": Color(1.0, 0.2, 0.2),
		"opin": Color(1.0, 0.2, 0.2),
		"label": Color(1.0, 1.0, 0.3),
		"resistor": Color(1.0, 0.6, 0.1),
		"capacitor": Color(0.4, 0.6, 1.0),
		"poly_resistor": Color(1.0, 0.6, 0.1),
		"unknown": Color(0.5, 0.5, 0.5),
		"wire": Color(0.9, 0.9, 0.9),
	}
	for type in defs:
		var mat = StandardMaterial3D.new()
		mat.albedo_color = defs[type]
		mat.emission_enabled = true
		mat.emission = defs[type]
		mat.emission_energy_multiplier = 0.3
		_materials[type] = mat


func _draw_circuit():
	for child in get_children():
		child.queue_free()

	for wire in parser.wires:
		_draw_wire(wire)

	for comp in parser.components:
		_draw_component(comp)


# ---------- Wires ----------

func _draw_wire(wire: Dictionary):
	var p1 = Vector3(wire.x1, 0, -wire.y1) * scale_factor
	var p2 = Vector3(wire.x2, 0, -wire.y2) * scale_factor

	var midpoint = (p1 + p2) / 2.0
	var direction = p2 - p1
	var length = direction.length()

	if length < 0.001:
		return

	var mi = MeshInstance3D.new()
	var box = BoxMesh.new()
	box.size = Vector3(length, 0.015, 0.015)
	mi.mesh = box
	mi.material_override = _materials["wire"]
	mi.position = midpoint
	mi.rotation.y = -atan2(direction.z, direction.x)
	add_child(mi)

	# Connection dots at both endpoints
	_draw_connection_dot(p1)
	_draw_connection_dot(p2)

	# Wire label at midpoint
	var label_text: String = wire.get("label", "")
	if label_text != "":
		var label = Label3D.new()
		label.text = label_text
		label.position = midpoint + Vector3(0, 0.04, 0)
		label.font_size = 10
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.modulate = Color(0.7, 0.7, 0.7)
		label.outline_size = 4
		add_child(label)


func _draw_connection_dot(pos: Vector3):
	var mi = MeshInstance3D.new()
	var sphere = SphereMesh.new()
	sphere.radius = 0.012
	sphere.height = 0.024
	mi.mesh = sphere
	mi.material_override = _materials["wire"]
	mi.position = pos
	add_child(mi)


# ---------- Components ----------

func _draw_component(comp: Dictionary):
	var pos = Vector3(comp.x, 0, -comp.y) * scale_factor
	var type: String = parser.get_component_type(comp.symbol)
	var rot: int = comp.get("rotation", 0)
	var mirror: int = comp.get("mirror", 0)

	# Get parsed SymbolDefinition (cached)
	var sym_def: SymbolDefinition = _get_sym_def(comp.symbol)

	# Determine material from the .sym type field, falling back to SchParser type
	var mat_type: String = sym_def.type if sym_def.type != "" else type
	var mat: StandardMaterial3D = _get_material(mat_type)

	# Create data-driven symbol
	var symbol = CircuitSymbol.new()
	symbol.setup(comp, sym_def, scale_factor, mat)
	symbol.position = pos
	symbol.rotation.y = deg_to_rad(rot * 90.0)
	if mirror:
		symbol.scale.x = -1.0
	add_child(symbol)

	# Label (added to self, not the rotated symbol, so text stays upright)
	var comp_label: String = comp.get("label", "")
	var label_text = mat_type
	if comp_label != "":
		label_text += " [%s]" % comp_label

	var label = Label3D.new()
	label.text = label_text
	label.position = pos + Vector3(0, _get_label_height(mat_type), 0)
	label.font_size = 10
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.modulate = mat.albedo_color
	label.outline_size = 4
	add_child(label)


# ---------- Symbol Data Resolution ----------

func _get_sym_def(symbol_name: String) -> SymbolDefinition:
	if _sym_cache.has(symbol_name):
		return _sym_cache[symbol_name]

	var path = _resolve_sym_path(symbol_name)
	var sym_def: SymbolDefinition
	if path == "":
		sym_def = SymbolDefinition.new()
	else:
		sym_def = SymParser.parse(path)

	_sym_cache[symbol_name] = sym_def
	return sym_def


func _resolve_sym_path(symbol_name: String) -> String:
	# symbol_name comes from .sch, e.g.:
	#   "sky130_fd_pr/nfet_01v8.sym"
	#   "ipin.sym"

	# Try full path in each search directory
	for search_path in _sym_search_paths:
		var candidate = search_path + symbol_name
		if FileAccess.file_exists(candidate):
			return candidate

	# Try just the filename in each search path
	var basename = symbol_name.get_file()
	for search_path in _sym_search_paths:
		var candidate = search_path + basename
		if FileAccess.file_exists(candidate):
			return candidate

	push_warning("SymParser: .sym file not found for: " + symbol_name)
	return ""


func _get_material(type: String) -> StandardMaterial3D:
	if _materials.has(type):
		return _materials[type]
	return _materials["unknown"]


func _get_label_height(type: String) -> float:
	match type:
		"pmos": return 0.08
		"nmos": return 0.08
		"resistor", "poly_resistor": return 0.08
		"label": return 0.04
		"ipin", "input_pin": return 0.06
		"opin", "output_pin": return 0.06
		_: return 0.07
