extends Camera3D


@export var speed: float = 400.0   # units per second
@export var zoom_speed: float = 300.0  # speed for zooming in/out
# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	pass # Replace with function body.


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
