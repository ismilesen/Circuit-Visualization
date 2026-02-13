class_name SymParser



######################################################################################################
static func parse(path: String) -> SymbolDefinition:
	var symbol = SymbolDefinition.new()
	var f = FileAccess.open(path, FileAccess.READ)
	
	while not f.eof_reached():
		var raw_line = f.get_line().strip_edges()
		
		if raw_line == "" or raw_line.begins_with("*"):
			continue
		
		if raw_line.begins_with("L "):
			_parse_line(raw_line, symbol)
			
		elif raw_line.begins_with("B "):
			_parse_box(raw_line, symbol)
		
		elif raw_line.begins_with("P "):
			_parse_polygon(raw_line, symbol)
			
		
	
	return symbol

######################################################################################################
# L layer# x1 y1 x2 y2 {}
static func _parse_line(raw_line: String, symbol: SymbolDefinition) -> void:
	var parts := raw_line.split(" ", false)
	
	var line := SymbolDefinition.Line.new()
	line.layer = parts[1].to_float()
	line.p1 = Vector2(parts[2].to_float(), parts[3].to_float())
	line.p2 = Vector2(parts[4].to_float(), parts[5].to_float())
	
	symbol.lines.append(line)
######################################################################################################
# B layer# x y width height {}
static func _parse_box(raw_line: String, symbol: SymbolDefinition) -> void:
	var parts := raw_line.split(" ", false)
	
	var box := SymbolDefinition.Box.new()
	box.layer = parts[1].to_float()
	box.p1 = Vector2(parts[2].to_float(), parts[3].to_float())
	box.p2 = Vector2(parts[4].to_float(), parts[5].to_float())
	
	symbol.boxes.append(box)

######################################################################################################
#P layer# numpoints x1 y1 x2 y2 ... {fill=true}
static func _parse_polygon(raw_line: String, symbol: SymbolDefinition) -> void:
	var parts := raw_line.split(" ", false)

	var polygon := SymbolDefinition.Polygon.new()
	
	var property_start := raw_line.find("{")
	var property := ""
	
	if property_start != -1:
		property = raw_line.substr(property_start).strip_edges() # includes {}
	
	polygon.layer = int(parts[1]) # first number after 'P'
	var numpoints = int(parts[2])
	polygon.points = []
	for i in range(3, (2 * numpoints) + 3, 2):
		var x := parts[i].to_float()
		var y := parts[i + 1].to_float()
		polygon.points.append(Vector2(x, y))
	
	if property != "":
		if property.find("fill=true") != -1:
			polygon.fill = true
	
	symbol.polygons.append(polygon)

######################################################################################################
