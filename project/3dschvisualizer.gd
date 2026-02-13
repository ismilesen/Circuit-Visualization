extends Node3D

var components = []
var nets = []
@export var offset = Vector2(400, 300)


# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	#var sym = SymParser.parse("res://symbols/sky130_fd_pr/pfet_01v8_hvt.sym")
	#draw_component_3d(Vector2(1,1), sym)
	
	
	parse_sch("res://schematics/inv/sky130_fd_sc_hd__inv_1.sch")
	for n in nets:
		draw_net_3d(n[0], n[1])
	for c in components:
		var sym_path = find_sym_file("res://symbols", c.sym.get_file())
		var sym_def = SymParser.parse(sym_path)
		draw_component_3d(c.pos, sym_def)
	
# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta: float) -> void:
	pass

######################################################################################################
func parse_sch(path):
	var f = FileAccess.open(path, FileAccess.READ)
	while not f.eof_reached():
		var line = f.get_line().strip_edges()
		# finds position and file name
		if line.begins_with("C "):
			var parts = line.split(" ")
			var sym = parts[1]
			sym = sym.trim_prefix("{").trim_suffix("}")
			var x = parts[2].to_float()
			var y = parts[3].to_float()
			components.append({"sym": sym, "pos": Vector2(x, -y)})
		elif line.begins_with("N "):
			var parts = line.split(" ")
			var x1 = parts[1].to_float()
			var y1 = parts[2].to_float()
			var x2 = parts[3].to_float()
			var y2 = parts[4].to_float()
			nets.append([Vector2(x1, -y1), Vector2(x2, -y2)])

######################################################################################################
func find_sym_file(base_path: String, filename: String) -> String:
	var dir = DirAccess.open(base_path)
	if dir == null:
		return ""
	
	dir.list_dir_begin()
	var file_name = dir.get_next()
	
	while file_name != "":
		var full_path = base_path + "/" + file_name
		
		if dir.current_is_dir():
			# Skip "." and ".."
			if file_name != "." and file_name != "..":
				var result = find_sym_file(full_path, filename)
				if result != "":
					return result
		else:
			if file_name == filename:
				return full_path
		
		file_name = dir.get_next()
	
	dir.list_dir_end()
	return ""
	
######################################################################################################
func draw_net_3d(a: Vector2, b: Vector2):
	var start = Vector3(a.x, a.y, 0.0)
	var end = Vector3(b.x, b.y, 0.0)
	var direction = end - start
	var length = start.distance_to(end)
	var mid = (start + end) / 2.0
	var mesh = BoxMesh.new()
	mesh.size = Vector3(2, 2, length)


	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.0, 1.0, 1.0, 1.0)
	mesh.material = material
	
	var net = MeshInstance3D.new()
	net.mesh = mesh
	net.position = mid
	
	add_child(net)
	net.look_at(end, Vector3.UP)
	

######################################################################################################
func draw_component_3d(pos: Vector2, sym: SymbolDefinition):
	for line in sym.lines:
		draw_line(pos, line)
	for box in sym.boxes:
		draw_box(pos, box)
	for poly in sym.polygons:
		draw_poly(pos, poly)


######################################################################################################
func draw_line(pos: Vector2, line: SymbolDefinition.Line):
	# flip y values because they are different in a sym file
	var start = Vector3(line.p1.x + pos.x, -line.p1.y + pos.y, 0.0)
	var end = Vector3(line.p2.x + pos.x, -line.p2.y + pos.y, 0.0)
	var direction = end - start
	var length = start.distance_to(end)
	var mid = (start + end) / 2.0
	var mesh = BoxMesh.new()
	mesh.size = Vector3(2, 2, length)
	
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.5, 1, 0.0)
	mesh.material = material
	
	var line_instance := MeshInstance3D.new()
	line_instance.mesh = mesh
	line_instance.position = mid
	
	add_child(line_instance)
	line_instance.look_at(end, Vector3.UP)
	

######################################################################################################
func draw_box(pos: Vector2, box: SymbolDefinition.Box):
	var width := box.p2.x - box.p1.x
	var height := box.p2.y - box.p1.y
	var depth := 2.0
	# flip y values because they are different in a sym file
	var center := Vector3(pos.x + (box.p2.x + box.p1.x) / 2, pos.y - ((box.p2.y + box.p1.y) / 2), 0.0)
	
	var mesh := BoxMesh.new()
	mesh.size = Vector3(width, height, depth)
	
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(1.0, 0.0, 0.0, 1.0)
	mesh.material = material

	var box_instance := MeshInstance3D.new()
	box_instance.mesh = mesh
	box_instance.position = center
	
	add_child(box_instance)
	
######################################################################################################
func draw_poly(pos: Vector2, poly: SymbolDefinition.Polygon):
	
	var points: Array[Vector2] = []
	for p in poly.points:
		points.append(Vector2(p.x + pos.x, -p.y + pos.y))
	points.remove_at(0) # remove repeated point
	
	var indices = Geometry2D.triangulate_polygon(points)
	var depth := 2.0
	
	
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	# top face, reversed culling to show from the -z direction
	for i in range(0, indices.size(), 3):
		var p0 = points[indices[i]]
		var p1 = points[indices[i + 1]]
		var p2 = points[indices[i + 2]]
		st.add_vertex(Vector3(p2.x, p2.y, depth / 2.0))
		st.add_vertex(Vector3(p1.x, p1.y, depth / 2.0))
		st.add_vertex(Vector3(p0.x, p0.y, depth / 2.0))
		
	# bottom face, normal to show from the z direction
	for i in range(0, indices.size(), 3):
		var p0 = points[indices[i]]
		var p1 = points[indices[i + 1]]
		var p2 = points[indices[i + 2]]
		st.add_vertex(Vector3(p0.x, p0.y, -depth / 2.0))
		st.add_vertex(Vector3(p1.x, p1.y, -depth / 2.0))
		st.add_vertex(Vector3(p2.x, p2.y, -depth / 2.0))
		

	# creates side walls, needs to be tested with dynamic camera
	for i in range(points.size()):
		var next = (i + 1) % points.size()
		
		var p0 = points[i]
		var p1 = points[next]
		
		var v0 = Vector3(p0.x, p0.y, depth / 2.0)
		var v1 = Vector3(p1.x, p1.y, depth / 2.0)
		var v2 = Vector3(p1.x, p1.y, -depth / 2.0)
		var v3 = Vector3(p0.x, p0.y, -depth / 2.0)
		
		# first triangle
		st.add_vertex(v0)
		st.add_vertex(v1)
		st.add_vertex(v2)
		
		# second triangle
		st.add_vertex(v0)
		st.add_vertex(v2)
		st.add_vertex(v3)

	st.generate_normals()
	
	var mesh = st.commit()
	
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(1.0, 0.0, 0.0, 1.0)
	mesh.surface_set_material(0, material)
	
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	
	add_child(instance)
	
