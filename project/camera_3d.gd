extends Camera3D


@export var speed: float = 100
@export var zoom_speed: float = 50  # speed for zooming in/out
@export var mouse_sensitivity: float = 0.3 # rotation sensitivity


var rotation_x := 0.0   # pitch
var rotation_y := 0.0   # yaw

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	pass # Replace with function body.
	

func _unhandled_input(event):
	if event is InputEventMouseMotion:
		rotation_y -= event.relative.x * mouse_sensitivity
		rotation_x -= event.relative.y * mouse_sensitivity
		rotation_x = clamp(rotation_x, -90, 90) # prevent flipping
		rotation_degrees = Vector3(rotation_x, rotation_y, 0)


# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	var input_vector := Vector2.ZERO
	
	if Input.is_action_pressed("ui_up") or Input.is_key_pressed(KEY_W):
		input_vector.y += 1
	if Input.is_action_pressed("ui_down") or Input.is_key_pressed(KEY_S):
		input_vector.y -= 1
	if Input.is_action_pressed("ui_left") or Input.is_key_pressed(KEY_A):
		input_vector.x -= 1
	if Input.is_action_pressed("ui_right") or Input.is_key_pressed(KEY_D):
		input_vector.x += 1
	if Input.is_key_pressed(KEY_Q):
		position.z += zoom_speed * delta #zoom out
	if Input.is_key_pressed(KEY_E):
		position.z -= zoom_speed * delta #zoom in
	
	# Normalize to avoid faster diagonal movement
	if input_vector.length() > 0:
		input_vector = input_vector.normalized()
	
	# Move in X-Y plane
	position.x += input_vector.x * speed * delta
	position.y += input_vector.y * speed * delta
