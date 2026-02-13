extends Label

@onready var visualizer := get_parent().get_node("visualizer")

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	pass # Replace with function body.


# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta: float) -> void:
	var mouse_local = get_local_mouse_position()
	# Convert to schematic coordinates
	var schematic = mouse_local - visualizer.offset
	schematic.y = -schematic.y
	text = "schematic: (%.1f, %.1f)" % [schematic.x, schematic.y]
