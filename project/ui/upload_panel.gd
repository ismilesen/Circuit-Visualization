extends Control

@export var simulator_path: NodePath = NodePath("..")
@export var auto_start_continuous: bool = false
@export var continuous_step: float = 1e-11
@export var continuous_window: float = 2e-8
@export var continuous_sleep_ms: int = 25
const UPLOAD_DIR := "user://uploads"

var NETLIST_EXTS: PackedStringArray = PackedStringArray(["spice", "cir", "net", "txt"])
var XSCHEM_EXTS: PackedStringArray = PackedStringArray(["sch"])

@onready var upload_button: Button = get_node_or_null("Margin/VBox/ControlsRow/UploadButton") as Button
@onready var run_button: Button = get_node_or_null("Margin/VBox/ControlsRow/RunButton") as Button
@onready var continuous_button: Button = get_node_or_null("Margin/VBox/ControlsRow/ContinuousButton") as Button
@onready var clear_button: Button = get_node_or_null("Margin/VBox/ControlsRow/ClearButton") as Button
@onready var staged_list: ItemList = get_node_or_null("Margin/VBox/StagedList") as ItemList
@onready var status_prefix: Label = get_node_or_null("Margin/VBox/StatusRow/StatusPrefix") as Label
@onready var status_value: Label = get_node_or_null("Margin/VBox/StatusRow/StatusValue") as Label
@onready var output_box: RichTextLabel = get_node_or_null("Margin/VBox/Output") as RichTextLabel
@onready var file_dialog: FileDialog = get_node_or_null("FileDialog") as FileDialog

@onready var drop_zone: PanelContainer = get_node_or_null("Margin/VBox/DropZone") as PanelContainer
@onready var drop_title: Label = get_node_or_null("Margin/VBox/DropZone/DropZoneMargin/DropZoneVBox/DropTitle") as Label
@onready var drop_hint: Label = get_node_or_null("Margin/VBox/DropZone/DropZoneMargin/DropZoneVBox/DropHint") as Label

# Each entry:
# { "display": String, "user_path": String, "bytes": int, "kind": String, "ext": String }
var staged: Array[Dictionary] = []
var _sim_signal_connected: bool = false
var _continuous_signal_connected: bool = false
var _sim: Node = null
var _continuous_frame_count: int = 0

# --- Aesthetic theme state (light, “Microsoft-esque”) ---
var _t: Theme = null
var _sb_panel: StyleBoxFlat = null
var _sb_panel_hover: StyleBoxFlat = null
var _sb_drop_idle: StyleBoxFlat = null
var _sb_drop_flash: StyleBoxFlat = null

enum StatusTone { IDLE, OK, WARN, ERROR }

func _on_native_file_selected(path: String) -> void:
	var normalized: String = _normalize_native_path(path)
	if normalized.is_empty():
		return
	var added: int = int(_stage_native_file(normalized))
	if added > 0:
		_flash_drop_zone()
		_refresh_status("native: staged 1 file", StatusTone.OK)


func _ready() -> void:
	if upload_button == null or run_button == null or continuous_button == null or clear_button == null or staged_list == null or status_prefix == null or status_value == null or file_dialog == null or drop_zone == null or drop_title == null or drop_hint == null:
		push_error("UploadPanel scene is missing required child nodes. Ensure res://ui/upload_panel.tscn matches upload_panel.gd paths.")
		return

	_apply_light_theme()
	_ensure_upload_dir()

	upload_button.pressed.connect(_on_upload_pressed)
	run_button.pressed.connect(_on_run_pressed)
	continuous_button.pressed.connect(_on_continuous_pressed)
	clear_button.pressed.connect(_on_clear_pressed)
	file_dialog.file_selected.connect(_on_native_file_selected)
	file_dialog.files_selected.connect(_on_native_files_selected)
	if not OS.has_feature("web"):
		# Keep desktop picker in OS-native mode and filesystem scope.
		file_dialog.use_native_dialog = true
		file_dialog.access = FileDialog.ACCESS_FILESYSTEM
		file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILES
		file_dialog.filters = PackedStringArray([
			"*.spice, *.cir, *.net, *.txt ; Netlists",
			"*.sch ; Xschem schematics",
			"* ; All files"
		])


	# OS drag-and-drop (IMPORTANT):
	# Docs show connecting via the main viewport for file drops. :contentReference[oaicite:2]{index=2}
	# On Windows, when running from the editor, drag-drop can be intercepted by the editor UI,
	# so the most reliable test is in an exported .exe.
	if not OS.has_feature("web"):
		if get_viewport() != null:
			if not get_viewport().files_dropped.is_connected(_on_os_files_dropped):
				get_viewport().files_dropped.connect(_on_os_files_dropped)

		var w: Window = get_window()
		if w != null:
			if not w.files_dropped.is_connected(_on_os_files_dropped):
				w.files_dropped.connect(_on_os_files_dropped)

		if Engine.is_editor_hint():
			_log("[color=yellow]Note:[/color] In the editor, Windows drag-drop often targets the editor window instead of the running game. Export and run the .exe to test OS drag-drop reliably.")

	_sim = _resolve_simulator()
	_refresh_status("idle", StatusTone.IDLE)

	if OS.has_feature("web"):
		var has_bridge: bool = _web_eval_bool("typeof window.godotUploadOpenPicker === 'function' && Array.isArray(window.godotUploadQueue)")
		if has_bridge:
			_log("[color=lime]Web upload bridge detected.[/color]")
		else:
			_log("[color=yellow]Web upload bridge not detected yet. Ensure upload_bridge.js is included in the exported HTML.[/color]")

func _process(_delta: float) -> void:
	if OS.has_feature("web"):
		_poll_web_queue()

# -------------------------------------------------------------------
# Upload flows
# -------------------------------------------------------------------

func _on_upload_pressed() -> void:
	if OS.has_feature("web"):
		var ok: bool = _web_eval_bool("typeof window.godotUploadOpenPicker === 'function'")
		if not ok:
			_set_error("Web picker not available. Did you include res://web/shell/upload_bridge.js in the export HTML?")
			return
		JavaScriptBridge.eval("window.godotUploadOpenPicker()", true)
		_refresh_status("web: picker opened", StatusTone.WARN)
	else:
		file_dialog.popup_centered_ratio(0.8)
		_refresh_status("native: file dialog opened", StatusTone.WARN)

func _on_native_files_selected(paths: PackedStringArray) -> void:
	if paths.is_empty():
		return
	var added: int = 0
	for p: String in paths:
		var normalized: String = _normalize_native_path(p)
		if normalized.is_empty():
			continue
		added += int(_stage_native_file(normalized))
	_flash_drop_zone()
	_refresh_status("native: staged %d file(s)" % added, StatusTone.OK)

func _on_os_files_dropped(files: PackedStringArray) -> void:
	# OS-level drag-and-drop from Explorer/Finder/etc.
	# Note: only works with native windows (main window / non-embedded). :contentReference[oaicite:3]{index=3}
	if OS.has_feature("web"):
		return
	if files.is_empty():
		return

	# Defensive: sometimes editor passes weird strings, ignore empties.
	var added: int = 0
	for p: String in files:
		var normalized: String = _normalize_native_path(p)
		if normalized.is_empty():
			continue
		added += int(_stage_native_file(normalized))

	if added > 0:
		_flash_drop_zone()
		_refresh_status("native: dropped %d file(s)" % added, StatusTone.OK)
	else:
		_refresh_status("native: drop received, no valid files", StatusTone.WARN)

func _stage_native_file(src_path: String) -> bool:
	var normalized_path: String = _normalize_native_path(src_path)
	if normalized_path.is_empty():
		_set_error("File selection returned an empty/null path.")
		return false

	if not FileAccess.file_exists(normalized_path):
		_set_error("File does not exist: %s" % normalized_path)
		return false

	var src: FileAccess = FileAccess.open(normalized_path, FileAccess.READ)
	if src == null:
		_set_error("Failed to open: %s" % normalized_path)
		return false

	var length: int = int(src.get_length())
	var bytes: PackedByteArray = src.get_buffer(length)
	src.close()

	# Some native pickers or wrappers can yield empty buffers unexpectedly.
	if bytes == null:
		bytes = PackedByteArray()
	if bytes.is_empty() and length > 0:
		var fallback_text: String = FileAccess.get_file_as_string(normalized_path)
		bytes = fallback_text.to_utf8_buffer()
		if bytes.is_empty():
			_set_error("Selected file read as null/empty bytes: %s" % normalized_path)
			return false

	var base_name: String = normalized_path.get_file()
	return _stage_bytes(base_name, bytes)

func _stage_bytes(original_name: String, bytes: PackedByteArray) -> bool:
	_ensure_upload_dir()

	var safe_name: String = _sanitize_filename(original_name)
	var user_path: String = "%s/%s" % [UPLOAD_DIR, safe_name]
	user_path = _avoid_collision(user_path)

	var f: FileAccess = FileAccess.open(user_path, FileAccess.WRITE)
	if f == null:
		_set_error("Failed to write into %s" % user_path)
		return false
	f.store_buffer(bytes)
	f.close()

	var ext: String = safe_name.get_extension().to_lower()
	var kind: String = _detect_kind(ext, bytes)

	var entry: Dictionary = {
		"display": safe_name,
		"user_path": user_path,
		"bytes": bytes.size(),
		"kind": kind,
		"ext": ext
	}
	staged.append(entry)
	_rebuild_list()
	_log("[color=lightblue]Staged[/color] %s  →  %s" % [safe_name, user_path])
	return true

# -------------------------------------------------------------------
# Web queue polling (JS -> Godot)
# -------------------------------------------------------------------

func _poll_web_queue() -> void:
	var raw: Variant = JavaScriptBridge.eval("""
		(() => {
			if (!Array.isArray(window.godotUploadQueue) || window.godotUploadQueue.length === 0) return null;
			const item = window.godotUploadQueue.shift();
			return JSON.stringify(item);
		})()
	""", true)

	if raw == null:
		return

	var json: String = str(raw)
	if json.is_empty():
		return

	var parsed: Variant = JSON.parse_string(json)
	if parsed == null or typeof(parsed) != TYPE_DICTIONARY:
		_set_error("Web upload: failed to parse queued JSON.")
		return

	var d: Dictionary = parsed as Dictionary
	if not d.has("name") or not d.has("base64"):
		_set_error("Web upload: queue item missing fields.")
		return

	if d.has("error") and str(d["error"]) != "":
		_set_error("Web upload error for %s: %s" % [str(d.get("name", "unknown")), str(d["error"])])
		return

	var filename: String = str(d.get("name", "upload.bin"))
	var b64: String = str(d.get("base64", ""))
	var bytes: PackedByteArray = Marshalls.base64_to_raw(b64)

	var ok: bool = _stage_bytes(filename, bytes)
	if ok:
		_flash_drop_zone()
		_refresh_status("web: staged %s (%s)" % [filename, _human_size(bytes.size())], StatusTone.OK)

func _web_eval_bool(expr: String) -> bool:
	var v: Variant = JavaScriptBridge.eval("(%s) ? true : false" % expr, true)
	return bool(v)

# -------------------------------------------------------------------
# Run simulation
# -------------------------------------------------------------------

func _on_run_pressed() -> void:
	if staged.is_empty():
		_set_error("No staged files. Upload a .spice/.cir/.net/.txt netlist first.")
		return

	var idx: PackedInt32Array = staged_list.get_selected_items()
	if idx.is_empty():
		_set_error("Select a staged netlist in the list first.")
		return

	var entry: Dictionary = staged[int(idx[0])]
	if not _is_netlist_entry(entry):
		_set_error("Selected file is not a netlist type. Choose .spice/.cir/.net/.txt.")
		return

	if OS.has_feature("web"):
		_set_error("Web build: ngspice runtime is not supported yet, staging works though.")
		return

	_sim = _resolve_simulator()
	if _sim == null:
		_set_error("Could not find CircuitSimulator node. Ensure the harness instanced it, or set simulator_path.")
		return

	if not _sim.has_method("initialize_ngspice"):
		_set_error("Resolved node lacks initialize_ngspice(). Wrong simulator_path?")
		return

	if (not _sim_signal_connected) and _sim.has_signal("simulation_finished"):
		_sim.connect("simulation_finished", Callable(self, "_on_sim_finished"))
		_sim_signal_connected = true
	if (not _continuous_signal_connected) and _sim.has_signal("continuous_transient_started") and _sim.has_signal("continuous_transient_stopped") and _sim.has_signal("continuous_transient_frame"):
		_sim.connect("continuous_transient_started", Callable(self, "_on_continuous_started"))
		_sim.connect("continuous_transient_stopped", Callable(self, "_on_continuous_stopped"))
		_sim.connect("continuous_transient_frame", Callable(self, "_on_continuous_frame"))
		_continuous_signal_connected = true

	_refresh_status("native: initializing ngspice…", StatusTone.WARN)
	var init_ok: Variant = _sim.call("initialize_ngspice")
	if not bool(init_ok):
		_set_error("initialize_ngspice() returned false.")
		return

	_refresh_status("native: loading netlist…", StatusTone.WARN)
	var godot_path: String = str(entry["user_path"]) # user://uploads/...
	var os_path: String = ProjectSettings.globalize_path(godot_path)

	# Prefer the normalized SPICE pipeline for uploaded decks.
	# It avoids .control/quit side-effects and handles include/path normalization.
	if _sim.has_method("run_spice_file"):
		_refresh_status("native: running spice pipeline…", StatusTone.WARN)
		var run_result: Variant = _sim.call("run_spice_file", os_path, "")
		if typeof(run_result) != TYPE_DICTIONARY:
			_set_error("run_spice_file() returned unexpected result.")
			return

		var result_dict: Dictionary = run_result as Dictionary
		if result_dict.is_empty():
			_set_error("run_spice_file() returned no data. Check ngspice output for deck errors.")
			return

		var key_count: int = result_dict.keys().size()
		_refresh_status("native: simulation complete (%d result fields)" % key_count, StatusTone.OK)
		_log("[color=lime]Simulation complete.[/color] Result keys: %s" % [str(result_dict.keys())])

		if auto_start_continuous and _sim.has_method("start_continuous_transient"):
			var started: bool = bool(_sim.call("start_continuous_transient", continuous_step, continuous_window, continuous_sleep_ms))
			if started:
				_refresh_status("native: continuous transient started", StatusTone.OK)
			else:
				_set_error("Failed to start continuous transient loop.")
		return

	# Fallback for older simulator builds that don't expose run_spice_file.
	_sim.call("load_netlist", os_path)
	if auto_start_continuous and _sim.has_method("start_continuous_transient"):
		var fallback_started: bool = bool(_sim.call("start_continuous_transient", continuous_step, continuous_window, continuous_sleep_ms))
		if fallback_started:
			_refresh_status("native: continuous transient started", StatusTone.OK)
			return
	_refresh_status("native: running simulation…", StatusTone.WARN)
	_sim.call("run_simulation")

func _on_continuous_pressed() -> void:
	if OS.has_feature("web"):
		_set_error("Web build: continuous ngspice mode is not supported.")
		return

	_sim = _resolve_simulator()
	if _sim == null:
		_set_error("Could not find CircuitSimulator node.")
		return
	if not _sim.has_method("start_continuous_transient") or not _sim.has_method("stop_continuous_transient"):
		_set_error("CircuitSimulator build does not expose continuous transient methods.")
		return

	if bool(_sim.call("is_continuous_transient_running")):
		_sim.call("stop_continuous_transient")
		_refresh_status("native: stopping continuous transient…", StatusTone.WARN)
		return

	# Ensure a deck is loaded before starting continuous chunks.
	_on_run_pressed()
	if _sim == null:
		return

	if bool(_sim.call("is_continuous_transient_running")):
		return
	var started: bool = bool(_sim.call("start_continuous_transient", continuous_step, continuous_window, continuous_sleep_ms))
	if not started:
		_set_error("Failed to start continuous transient loop.")

func _on_sim_finished() -> void:
	_refresh_status("native: simulation_finished", StatusTone.OK)
	_log("[color=lime]Simulation finished.[/color]")

func _on_continuous_started() -> void:
	call_deferred("_apply_continuous_started_ui")

func _on_continuous_stopped() -> void:
	call_deferred("_apply_continuous_stopped_ui")

func _on_continuous_frame(frame: Dictionary) -> void:
	call_deferred("_apply_continuous_frame_ui", frame)

func _apply_continuous_started_ui() -> void:
	_continuous_frame_count = 0
	if continuous_button != null:
		continuous_button.text = "Stop Continuous"
	_refresh_status("native: continuous transient running", StatusTone.OK)
	_log("[color=lime]Continuous transient started.[/color]")

func _apply_continuous_stopped_ui() -> void:
	if continuous_button != null:
		continuous_button.text = "Start Continuous"
	_refresh_status("native: continuous transient stopped", StatusTone.WARN)
	_log("[color=yellow]Continuous transient stopped.[/color]")

func _apply_continuous_frame_ui(frame: Dictionary) -> void:
	_continuous_frame_count += 1
	if _continuous_frame_count % 10 != 0:
		return

	var chunk_start: float = float(frame.get("chunk_start", 0.0))
	var chunk_stop: float = float(frame.get("chunk_stop", 0.0))
	_refresh_status(
		"native: continuous chunk %d (%.3e → %.3e s)" % [_continuous_frame_count, chunk_start, chunk_stop],
		StatusTone.OK
	)

# -------------------------------------------------------------------
# Clear staging
# -------------------------------------------------------------------

func _on_clear_pressed() -> void:
	if _sim != null and _sim.has_method("is_continuous_transient_running") and bool(_sim.call("is_continuous_transient_running")):
		_sim.call("stop_continuous_transient")
	staged.clear()
	if staged_list != null:
		staged_list.clear()
	if output_box != null:
		output_box.clear()
	if run_button != null:
		run_button.text = "Run Once"
	if continuous_button != null:
		continuous_button.text = "Start Continuous"
	_refresh_status("staging cleared", StatusTone.WARN)

# -------------------------------------------------------------------
# Helpers
# -------------------------------------------------------------------

func _resolve_simulator() -> Node:
	if simulator_path != NodePath("") and has_node(simulator_path):
		var n0: Node = get_node(simulator_path)
		if n0 != null and n0.has_method("initialize_ngspice"):
			return n0

	var cur: Node = self
	while cur != null:
		if cur.has_method("initialize_ngspice") and cur.has_method("load_netlist") and cur.has_method("run_simulation"):
			return cur
		cur = cur.get_parent()

	var root: Window = get_tree().root
	if root != null:
		var candidates: Array = root.find_children("*", "", true, false)
		for c in candidates:
			if c is Node and (c as Node).has_method("initialize_ngspice") and (c as Node).has_method("load_netlist"):
				return c as Node

	return null

func _ensure_upload_dir() -> void:
	var abs_path: String = ProjectSettings.globalize_path(UPLOAD_DIR)
	DirAccess.make_dir_recursive_absolute(abs_path)

func _sanitize_filename(filename: String) -> String:
	var s: String = filename.strip_edges()
	s = s.replace("\\", "_").replace("/", "_").replace(":", "_")
	s = s.replace("*", "_").replace("?", "_").replace("\"", "_").replace("<", "_").replace(">", "_").replace("|", "_")
	if s == "":
		s = "upload.bin"
	return s

func _avoid_collision(user_path: String) -> String:
	if not FileAccess.file_exists(user_path):
		return user_path

	var base: String = user_path.get_basename()
	var ext: String = user_path.get_extension()
	var stamp: int = int(Time.get_unix_time_from_system())
	return "%s_%d.%s" % [base, stamp, ext]

func _normalize_native_path(raw_path: String) -> String:
	var s: String = raw_path.strip_edges()
	if s.is_empty():
		return ""
	if s.to_lower() == "null":
		return ""
	if s.begins_with("file://"):
		s = s.trim_prefix("file://")
		if s.begins_with("localhost/"):
			s = s.trim_prefix("localhost/")
		# macOS/Linux absolute file URI gives one leading slash in URI payload.
		if s.length() >= 3 and s.begins_with("/") and s.substr(2, 1) == ":":
			# Windows URI like /C:/...
			s = s.substr(1)
		s = s.uri_decode()
	return s

func _detect_kind(ext: String, bytes: PackedByteArray) -> String:
	if XSCHEM_EXTS.has(ext):
		return "xschem (.sch)"
	if NETLIST_EXTS.has(ext):
		var head: String = _bytes_head_as_text(bytes, 120).strip_edges()
		if head.begins_with(".") or head.begins_with("*") or head.find("ngspice") != -1:
			return "netlist"
		return "netlist/text"
	return "unknown"

func _is_netlist_entry(entry: Dictionary) -> bool:
	return entry.has("ext") and NETLIST_EXTS.has(str(entry["ext"]))

func _bytes_head_as_text(bytes: PackedByteArray, n: int) -> String:
	var slice: PackedByteArray = bytes.slice(0, min(n, bytes.size()))
	return slice.get_string_from_utf8()

func _rebuild_list() -> void:
	if staged_list == null:
		return
	staged_list.clear()
	for e in staged:
		var label: String = "%s    (%s, %s)    → %s" % [
			str(e["display"]),
			str(e["kind"]),
			_human_size(int(e["bytes"])),
			str(e["user_path"])
		]
		staged_list.add_item(label)

func _human_size(n: int) -> String:
	if n < 1024:
		return "%d B" % n
	if n < 1024 * 1024:
		return "%.1f KB" % (float(n) / 1024.0)
	return "%.2f MB" % (float(n) / (1024.0 * 1024.0))

func _refresh_status(msg: String, tone: StatusTone = StatusTone.IDLE) -> void:
	if status_prefix == null or status_value == null:
		return

	status_prefix.text = "Status:"
	status_prefix.add_theme_color_override("font_color", Color(1, 1, 1, 1))

	status_value.text = msg

	var c: Color
	match tone:
		StatusTone.OK:
			c = Color(0.25, 0.85, 0.45) # green
		StatusTone.WARN:
			c = Color(1.00, 0.70, 0.20) # orange
		StatusTone.ERROR:
			c = Color(1.00, 0.30, 0.30) # red
		_:
			c = Color(0.85, 0.85, 0.85) # light gray idle

	status_value.add_theme_color_override("font_color", c)

func _set_error(msg: String) -> void:
	_refresh_status("error", StatusTone.ERROR)
	_log("[color=tomato][b]Error:[/b][/color] %s" % msg)

func _log(bb: String) -> void:
	if output_box != null:
		output_box.append_text(bb + "\n")
		output_box.scroll_to_line(output_box.get_line_count())
	else:
		print_rich(bb)

# -------------------------------------------------------------------
# Styling: light “Microsoft-esque” theme + drop flash
# -------------------------------------------------------------------

func _apply_light_theme() -> void:
	_t = Theme.new()

	var bg: Color = Color(1, 1, 1)
	var panel: Color = Color(0.98, 0.98, 0.98)
	var border: Color = Color(0.82, 0.82, 0.82)
	var text: Color = Color(0.13, 0.13, 0.13)
	var subtext: Color = Color(0.35, 0.35, 0.35)
	var accent: Color = Color(0.00, 0.47, 0.83)
	var accent_hover: Color = Color(0.00, 0.40, 0.72)

	_sb_panel = StyleBoxFlat.new()
	_sb_panel.bg_color = panel
	_sb_panel.border_color = border
	_sb_panel.border_width_left = 1
	_sb_panel.border_width_top = 1
	_sb_panel.border_width_right = 1
	_sb_panel.border_width_bottom = 1
	_sb_panel.corner_radius_top_left = 10
	_sb_panel.corner_radius_top_right = 10
	_sb_panel.corner_radius_bottom_left = 10
	_sb_panel.corner_radius_bottom_right = 10
	_sb_panel.content_margin_left = 10
	_sb_panel.content_margin_right = 10
	_sb_panel.content_margin_top = 10
	_sb_panel.content_margin_bottom = 10

	_sb_panel_hover = _sb_panel.duplicate() as StyleBoxFlat
	_sb_panel_hover.border_color = Color(0.70, 0.70, 0.70)

	_sb_drop_idle = _sb_panel.duplicate() as StyleBoxFlat
	_sb_drop_idle.bg_color = Color(0.985, 0.985, 0.985)

	_sb_drop_flash = _sb_panel.duplicate() as StyleBoxFlat
	_sb_drop_flash.bg_color = Color(0.93, 0.97, 1.0)
	_sb_drop_flash.border_color = Color(0.35, 0.62, 0.90)

	var sb_root: StyleBoxFlat = StyleBoxFlat.new()
	sb_root.bg_color = bg
	add_theme_stylebox_override("panel", sb_root)

	_t.set_color("font_color", "Label", text)
	_t.set_color("font_color", "RichTextLabel", text)
	_t.set_color("font_color", "LineEdit", text)
	_t.set_color("font_color", "ItemList", text)

	drop_hint.add_theme_color_override("font_color", subtext)

	var sb_btn: StyleBoxFlat = StyleBoxFlat.new()
	sb_btn.bg_color = accent
	sb_btn.border_color = accent
	sb_btn.corner_radius_top_left = 8
	sb_btn.corner_radius_top_right = 8
	sb_btn.corner_radius_bottom_left = 8
	sb_btn.corner_radius_bottom_right = 8
	sb_btn.content_margin_left = 12
	sb_btn.content_margin_right = 12
	sb_btn.content_margin_top = 8
	sb_btn.content_margin_bottom = 8

	var sb_btn_hover: StyleBoxFlat = sb_btn.duplicate() as StyleBoxFlat
	sb_btn_hover.bg_color = accent_hover
	sb_btn_hover.border_color = accent_hover

	var sb_btn_pressed: StyleBoxFlat = sb_btn.duplicate() as StyleBoxFlat
	sb_btn_pressed.bg_color = Color(0.00, 0.34, 0.62)
	sb_btn_pressed.border_color = sb_btn_pressed.bg_color

	_t.set_stylebox("normal", "Button", sb_btn)
	_t.set_stylebox("hover", "Button", sb_btn_hover)
	_t.set_stylebox("pressed", "Button", sb_btn_pressed)
	_t.set_stylebox("focus", "Button", sb_btn_hover)
	_t.set_color("font_color", "Button", Color(1, 1, 1))

	var sb_edit: StyleBoxFlat = _sb_panel.duplicate() as StyleBoxFlat
	sb_edit.bg_color = Color(1, 1, 1)
	sb_edit.corner_radius_top_left = 8
	sb_edit.corner_radius_top_right = 8
	sb_edit.corner_radius_bottom_left = 8
	sb_edit.corner_radius_bottom_right = 8
	_t.set_stylebox("normal", "LineEdit", sb_edit)
	_t.set_stylebox("focus", "LineEdit", _sb_panel_hover)

	# ItemList / Output panes
	var sb_list: StyleBoxFlat = _sb_panel.duplicate() as StyleBoxFlat
	sb_list.bg_color = Color(1, 1, 1)
	_t.set_stylebox("panel", "ItemList", sb_list)
	_t.set_stylebox("normal", "RichTextLabel", sb_list)

	# ------------------------------------------------------------
	# ItemList hover + selection colors
	# Theme keys for ItemList include:
	#   StyleBoxes: hovered, selected, selected_focus, hovered_selected, hovered_selected_focus
	#   Colors: font_color, font_hovered_color, font_selected_color, font_hovered_selected_color
	# ------------------------------------------------------------

	# Hovered (darkish grey)
	var sb_item_hover: StyleBoxFlat = StyleBoxFlat.new()
	sb_item_hover.bg_color = Color(0.25, 0.25, 0.25, 0.55)   # dark grey hover
	sb_item_hover.corner_radius_top_left = 6
	sb_item_hover.corner_radius_top_right = 6
	sb_item_hover.corner_radius_bottom_left = 6
	sb_item_hover.corner_radius_bottom_right = 6
	sb_item_hover.content_margin_left = 6
	sb_item_hover.content_margin_right = 6
	sb_item_hover.content_margin_top = 2
	sb_item_hover.content_margin_bottom = 2

	# Selected (green)
	var sb_item_selected: StyleBoxFlat = sb_item_hover.duplicate() as StyleBoxFlat
	sb_item_selected.bg_color = Color(0.20, 0.75, 0.35, 0.90)  # green selection

	# Hovered + selected (slightly darker green)
	var sb_item_hover_selected: StyleBoxFlat = sb_item_selected.duplicate() as StyleBoxFlat
	sb_item_hover_selected.bg_color = Color(0.16, 0.68, 0.31, 0.95)

	# Apply the styleboxes to ItemList’s theme slots
	_t.set_stylebox("hovered", "ItemList", sb_item_hover)
	_t.set_stylebox("selected", "ItemList", sb_item_selected)
	_t.set_stylebox("selected_focus", "ItemList", sb_item_selected)
	_t.set_stylebox("hovered_selected", "ItemList", sb_item_hover_selected)
	_t.set_stylebox("hovered_selected_focus", "ItemList", sb_item_hover_selected)

	# Text colors for the different ItemList states
	_t.set_color("font_color", "ItemList", Color(0.13, 0.13, 0.13))                 # default
	_t.set_color("font_hovered_color", "ItemList", Color(1, 1, 1))                  # hover text
	_t.set_color("font_selected_color", "ItemList", Color(1, 1, 1))                 # selected text
	_t.set_color("font_hovered_selected_color", "ItemList", Color(1, 1, 1))         # hover+selected text


	_t.set_stylebox("panel", "PanelContainer", _sb_panel)

	theme = _t
	drop_zone.add_theme_stylebox_override("panel", _sb_drop_idle)

func _flash_drop_zone() -> void:
	drop_zone.add_theme_stylebox_override("panel", _sb_drop_flash)
	drop_title.text = "Dropped, staging…"
	await get_tree().create_timer(0.35).timeout
	drop_zone.add_theme_stylebox_override("panel", _sb_drop_idle)
	drop_title.text = "Drop files here"
