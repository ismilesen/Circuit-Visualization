extends Node2D

var components = []
var nets = []
@export var offset = Vector2(400, 300)

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	parse_sch("res://inv.sch")
	
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

func _draw():
	for n in nets:
		draw_line(n[0] + offset, n[1] + offset, Color(0.7,0.7,0.7), 2)
	for c in components:
		draw_circle(c + offset, 6, Color.WHITE)
