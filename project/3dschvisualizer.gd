extends Node3D

var components = []
var nets = []
@export var offset = Vector2(400, 300)

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	parse_sch("res://inv.sch")
	for n in nets:
		draw_net_3d(n[0], n[1])
	for c in components:
		draw_component_3d(c)
	
# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta: float) -> void:
	pass

func parse_sch(path):
	var f = FileAccess.open(path, FileAccess.READ)
	while not f.eof_reached():
		var line = f.get_line().strip_edges()
		if line.begins_with("C "):
			var parts = line.split(" ")
			var x = parts[2].to_float()
			var y = parts[3].to_float()
			components.append(Vector2(x, -y))
		elif line.begins_with("N "):
			var parts = line.split(" ")
			var x1 = parts[1].to_float()
			var y1 = parts[2].to_float()
			var x2 = parts[3].to_float()
			var y2 = parts[4].to_float()
			nets.append([Vector2(x1, -y1), Vector2(x2, -y2)])


func draw_net_3d(a: Vector2, b: Vector2):
	var start = Vector3(a.x, a.y, 0.0)
	var end   = Vector3(b.x, b.y, 0.0)
	var length = start.distance_to(end)
	var mid = (start + end) / 2.0
	print(mid)
	var mesh = BoxMesh.new()
	mesh.size = Vector3(length, 10, 10)

	var net = MeshInstance3D.new()
	net.mesh = mesh
	net.position = mid
	# rotate to match direction
	add_child(net)
	

func draw_component_3d(pos: Vector2):
	var mesh = SphereMesh.new()
	mesh.radius = 25
	mesh.height = 25
	var comp = MeshInstance3D.new()
	comp.mesh = mesh
	comp.position = Vector3(pos.x, pos.y, 0.2)
	add_child(comp)
