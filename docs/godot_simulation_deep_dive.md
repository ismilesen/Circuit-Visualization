# Godot Simulation Deep Dive

This document explains the Godot-side simulation stack in depth:
- scene composition
- script/class responsibilities
- simulation runtime flow
- UI + signal lifecycle
- upload/staging behavior
- continuous transient control and CSV integration

This complements the native-extension deep dive in `docs/circuit_sim_deep_dive.md`.

## 1. Godot-Side Architecture

## 1.1 Runtime scenes

1. `res://ui/upload_harness.tscn`
- Root node: `UploadHarness` (`Node`) with script `res://ui/upload_harness.gd`.
- Child: `UILayer` (`CanvasLayer`) where upload/simulation UI is instanced.
- Role: bootstraps and wires simulator scene + UI scene together.

2. `res://circuit_simulator.tscn`
- Root node: `CircuitSimulator` (GDExtension C++ node).
- Role: native simulation backend (ngspice engine wrapper).

3. `res://ui/upload_panel.tscn`
- Root node: `UploadPanel` (`Control`) with script `res://ui/upload_panel.gd`.
- Contains all user-facing controls: upload, run once, continuous toggle, clear, status, log, file dialog.
- Role: orchestrates file ingest, simulator calls, runtime status, and CSV behavior.

## 1.2 Script classes (Godot-side)

1. `upload_harness.gd` (`extends Node`)
- Minimal composition/wiring script.
- Instantiates simulator + UI scenes and sets `simulator_path` on the panel.

2. `upload_panel.gd` (`extends Control`)
- Main controller for the simulation UX.
- Owns staged file metadata, upload handling, simulation commands, signal callbacks, and custom theme.

## 2. `upload_harness.gd` Deep Dive

### `const SIM_SCENE_PATH`, `const UI_SCENE_PATH`
- Declarative paths for the simulator scene and panel scene.

### `var sim_instance: Node`
- Stores instantiated native simulator node for wiring.

### `func _ready()`
- Calls `_instance_simulator_scene()` first, then `_instance_ui()`.
- Order matters: UI gets a valid `simulator_path` once simulator exists.

### `func _instance_simulator_scene()`
- Loads `res://circuit_simulator.tscn`.
- Instantiates and adds as a child of harness root.
- On failure, logs `push_error` and exits early.

### `func _instance_ui()`
- Loads `res://ui/upload_panel.tscn` and instances it under `UILayer`.
- Verifies `UILayer` exists.
- If UI has `simulator_path` and simulator exists, writes relative path:
  - `ui.set("simulator_path", ui.get_path_to(sim_instance))`
- This avoids hardcoded scene-tree assumptions in the panel script.

## 3. `upload_panel.gd` Deep Dive

## 3.1 Configuration exports

- `simulator_path`: preferred direct path to `CircuitSimulator` node.
- `auto_start_continuous`: auto-enter continuous mode right after a run.
- `continuous_step`, `continuous_window`, `continuous_sleep_ms`: continuous transient parameters.
- `continuous_csv_enabled`, `continuous_csv_path`, `continuous_csv_signals`:
  - toggles CSV export
  - target output path
  - optional signal filter (`PackedStringArray`)

These are editor-configurable and define runtime behavior without changing script code.

## 3.2 Onready bindings and state

UI nodes are resolved via explicit paths (`get_node_or_null`) for fail-fast checks in `_ready()`.

Core state:
- `staged: Array[Dictionary]`: staged file records.
- `_sim`: resolved simulator node.
- `_sim_signal_connected`, `_continuous_signal_connected`: prevent duplicate signal connections.
- `_continuous_frame_count`: used to throttle status updates for continuous frames.

Staged entry schema:
- `display`, `user_path`, `bytes`, `kind`, `ext`.

## 3.3 Lifecycle and setup

### `_ready()`
Responsibilities:
1. Validate required child nodes exist.
2. Apply custom UI theme (`_apply_light_theme`).
3. Ensure `user://uploads` exists (`_ensure_upload_dir`).
4. Connect button/dialog signals.
5. Configure native file picker in desktop builds.
6. Connect OS drag-and-drop signals in desktop builds.
7. Resolve simulator (`_resolve_simulator`) and initialize status.
8. In web builds, detect upload bridge readiness.

### `_process(_delta)`
- Web-only polling of JS upload queue via `_poll_web_queue()`.

## 3.4 Upload and staging subsystem

### Entry points
- `_on_upload_pressed()`:
  - web: invokes JS upload picker
  - desktop: opens native `FileDialog`
- `_on_native_file_selected(path)` and `_on_native_files_selected(paths)`.
- `_on_os_files_dropped(files)` for drag-and-drop payloads.

### File staging pipeline

1. `_stage_native_file(src_path)`:
- normalize path
- file existence/open checks
- read bytes (with string fallback if unexpectedly empty)
- pass to `_stage_bytes`

2. `_stage_bytes(original_name, bytes)`:
- ensure upload dir
- sanitize filename (`_sanitize_filename`)
- avoid collisions (`_avoid_collision`)
- write to `user://uploads`
- classify type (`_detect_kind`)
- append metadata to `staged`
- rebuild visible list (`_rebuild_list`)

### Web queue path

- `_poll_web_queue()` reads one queued JSON item from JS bridge.
- Decodes base64 payload and routes through `_stage_bytes`.
- `_web_eval_bool()` is a small helper for JS capability checks.

## 3.5 Simulation control subsystem

### `_on_run_pressed()`
High-level behavior:
1. Validate staged selection and file type.
2. Block on web builds for simulation execution (staging-only mode).
3. Resolve simulator and verify required methods.
4. Connect simulator signals once:
- `simulation_finished`
- `continuous_transient_started`
- `continuous_transient_stopped`
- `continuous_transient_frame`
- optional `continuous_csv_export_error`
5. Initialize ngspice (`initialize_ngspice`).
6. Convert selected `user://` path to OS path (`ProjectSettings.globalize_path`).
7. Prefer normalized pipeline (`run_spice_file`) if available.
8. If `auto_start_continuous` is enabled:
- configure CSV export if enabled
- call `start_continuous_transient`
9. Fallback path for old simulator builds:
- `load_netlist`
- optional continuous start
- otherwise `run_simulation`

### `_on_continuous_pressed()`
Toggle behavior:
- If running: stop continuous + disable CSV export.
- If not running:
  - call `_on_run_pressed()` to ensure deck is loaded
  - if still not running, configure CSV and start continuous loop.

### `_on_clear_pressed()`
- Stops continuous if active.
- Disables CSV export.
- Clears staged list and output log.
- Resets button texts and status.

### `_configure_csv_export_if_enabled()`
- If disabled by setting: calls native `disable_continuous_csv_export`.
- Else calls native `configure_continuous_csv_export(absolute_path, signal_filter)`.
- Logs configured path or reports error.

## 3.6 Simulator signal handling

Signals are intentionally deferred before UI mutation to avoid cross-thread UI updates:
- `_on_continuous_started()` -> `_apply_continuous_started_ui()`
- `_on_continuous_stopped()` -> `_apply_continuous_stopped_ui()`
- `_on_continuous_frame(frame)` -> `_apply_continuous_frame_ui(frame)`
- `_on_continuous_csv_export_error(message)` -> `_apply_continuous_csv_export_error_ui(message)`

Other handlers:
- `_on_sim_finished()` updates status/log for one-shot completion.

Continuous frame UI policy:
- updates every 10th frame only (status throttling)
- reads `chunk_start` and `chunk_stop` metadata from frame dictionary.

## 3.7 Simulator discovery and utilities

### `_resolve_simulator()`
Search strategy (in order):
1. Use exported `simulator_path` if valid and method-capable.
2. Walk parent chain for node with expected simulator methods.
3. Search scene tree descendants from root for compatible node.

This makes the panel resilient to scene composition changes.

### Utility functions

- `_ensure_upload_dir`: creates `user://uploads` recursively.
- `_sanitize_filename`: strips unsafe path characters.
- `_avoid_collision`: timestamp suffix if target path exists.
- `_normalize_native_path`: handles `file://`, `localhost/`, Windows URI edge cases.
- `_detect_kind`: lightweight content+extension classification.
- `_is_netlist_entry`: extension-based runnable filter.
- `_bytes_head_as_text`: quick text sniffing helper.
- `_rebuild_list`: refreshes staged `ItemList` rows.
- `_human_size`: byte formatting.
- `_refresh_status`, `_set_error`, `_log`: status and logging pipeline.

## 3.8 Styling subsystem

### `_apply_light_theme()`
Creates a runtime `Theme` and configures:
- panel/button/list styleboxes
- hover/selected states for `ItemList`
- color palette and typography colors
- drop-zone idle/flash styleboxes

### `_flash_drop_zone()`
- Temporarily swaps drop-zone stylebox.
- Updates drop title text for 350 ms.
- Restores idle style and label.

## 4. Godot-to-Native Call Contract

The panel script expects a node that supports methods/signals exposed by native `CircuitSimulator`.

Required methods for core run:
- `initialize_ngspice`
- `load_netlist` (fallback)
- `run_simulation` (fallback)

Preferred methods for modern path:
- `run_spice_file`
- `start_continuous_transient`
- `stop_continuous_transient`
- `is_continuous_transient_running`
- `configure_continuous_csv_export`
- `disable_continuous_csv_export`

Signals consumed:
- `simulation_finished`
- `continuous_transient_started`
- `continuous_transient_stopped`
- `continuous_transient_frame`
- `continuous_csv_export_error` (optional)

## 5. Runtime Step-Through (Godot Side)

## 5.1 Desktop simulation path

1. User uploads netlist (dialog or drag/drop).
2. File is copied into `user://uploads` and appears in staged list.
3. User selects staged item and clicks `Run Once`.
4. Panel resolves simulator and initializes ngspice.
5. Panel runs `run_spice_file(os_path, "")`.
6. If continuous enabled:
- panel configures CSV export
- panel starts continuous transient loop
7. Simulator emits frame/lifecycle signals.
8. Panel updates status/log and button state.

## 5.2 Continuous stop path

1. User clicks `Start Continuous` while active.
2. Panel calls `stop_continuous_transient`.
3. Panel disables CSV export.
4. Status changes to stopping/stopped and button text resets.

## 5.3 Web upload-only path

1. User clicks upload.
2. JS bridge opens browser picker.
3. `_process` polls queue, decodes base64, stages files.
4. Simulation run path is blocked with explicit web warning.

## 6. Design Tradeoffs and Practical Notes

- The panel intentionally uses capability checks (`has_method`, `has_signal`) to remain compatible with older/newer extension builds.
- UI updates from continuous simulation are deferred to avoid thread-safety issues.
- Path normalization and collision handling make uploads deterministic and safe.
- Continuous status throttling prevents excessive UI churn.
- CSV behavior is configured at UI level, but file writing occurs in the native simulator thread.

## 7. Where to Modify What

- Change runtime composition/wiring: `project/ui/upload_harness.gd`.
- Change user simulation workflow and behavior: `project/ui/upload_panel.gd`.
- Change panel structure/nodes: `project/ui/upload_panel.tscn`.
- Change native simulation engine behavior: `src/circuit_sim.cpp` and `src/circuit_sim.h`.
