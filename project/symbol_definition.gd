class_name SymbolDefinition

var lines: Array = []
var boxes: Array = []
var polygons: Array = []


class Line:
	var layer: int = 0
	var p1: Vector2
	var p2: Vector2
	
class Polygon:
	var layer: int = 0
	var points: Array[Vector2] = []
	var fill: bool = false

class Box:
	var layer: int = 0
	var p1: Vector2
	var p2: Vector2
