# Circuit Simulator Deep Dive

This document explains the `CircuitSimulator` implementation in depth:
- classes in this module
- every function in `src/circuit_sim.cpp`
- runtime behavior and threading
- a simple inverter walkthrough using `external` controls
- example CSV output

## Class Inventory

The native extension module currently exposes one runtime class:

1. `godot::CircuitSimulator` (`src/circuit_sim.h`, `src/circuit_sim.cpp`)

There are no additional user-facing C++ classes in this module. The rest of the file-level logic is implemented as private helper functions and ngspice callback functions.

## `CircuitSimulator` in Depth

`CircuitSimulator` is a Godot `Node` that embeds ngspice (`libngspice`) and provides:
- netlist loading (`source`, in-memory, normalized SPICE file path)
- simulation execution (run, tran, dc, background commands)
- rolling continuous transient windows for animation
- external source control (`external` flag workflows)
- optional CSV streaming from continuous frames (`time,signal,value`)

### Internal state and responsibilities

- `initialized`: whether ngspice has been loaded and initialized.
- `current_netlist`: last loaded netlist text/path (for visibility/debugging).
- ngspice function pointers: dynamically resolved symbols used for command and data APIs.
- `voltage_sources` + `voltage_sources_mutex`: thread-safe store for external source values requested by ngspice sync callbacks.
- `ng_command_mutex`: serializes command calls to ngspice.
- Continuous loop fields (`continuous_thread`, flags, step/window/next time): maintains rolling transient worker lifecycle.
- CSV fields (`csv_stream`, enabled/path/filter/last time): manages optional row-based stream export while continuous mode runs.

## Public API: Function-by-Function

### Lifecycle and init

1. `CircuitSimulator::CircuitSimulator()`
- Initializes all function pointers and runtime flags.
- Sets default continuous timing values and CSV disabled state.
- Assigns `CircuitSimulator::instance` for static callbacks.

2. `CircuitSimulator::~CircuitSimulator()`
- Stops continuous worker thread.
- Disables and closes CSV export.
- Shuts down ngspice if initialized.
- Clears static callback instance pointer.

3. `bool initialize_ngspice()`
- Loads ngspice shared library and resolves symbols.
- Registers ngspice callbacks (`ngSpice_Init`).
- Registers sync callbacks for external voltage/current sources (`ngSpice_Init_Sync`).
- Returns `true` on success, `false` on load/init failure.

4. `void shutdown_ngspice()`
- Stops continuous loop and CSV export.
- Halts background simulation and resets ngspice state.
- Unloads shared library handle.
- Marks simulator uninitialized.

5. `bool is_initialized() const`
- Returns current initialization state.

### Circuit loading

6. `bool load_netlist(const String &netlist_path)`
- Sends `source <path>` command to ngspice.
- Stores path as `current_netlist`.
- Returns success/failure.

7. `bool load_netlist_string(const String &netlist_content)`
- Splits provided text into lines and calls `ngSpice_Circ`.
- Stores text as `current_netlist`.
- Returns success/failure.

8. `Dictionary run_spice_file(const String &spice_path, const String &pdk_root = "")`
- Reads and normalizes a `.spice` file in memory.
- Rewrites include/lib/input_file paths to absolute paths.
- Strips `.control ... .endc`, extracts `tran` and `wrdata` intent.
- Injects generated `.save` line and `.end` when needed.
- Loads normalized lines via `ngSpice_Circ`, runs `run`, and collects vectors.
- Returns dictionary containing `time`, extracted vectors, `normalized_netlist`, and `signal_count`.

9. `String get_current_netlist() const`
- Returns last loaded or normalized netlist content/path.

### Simulation control

10. `bool run_simulation()`
- Executes `bg_run` in ngspice (background run).

11. `bool run_transient(double step, double stop, double start = 0.0)`
- Runs one transient command chunk (`tran step stop start`).

12. `bool run_dc(const String &source, double start, double stop, double step)`
- Runs a DC sweep command.

13. `void pause_simulation()`
- Sends `bg_halt`.

14. `void resume_simulation()`
- Sends `bg_resume`.

15. `void stop_simulation()`
- Sends `bg_halt` and logs stop message.

16. `bool is_running() const`
- Returns ngspice background running state (`ngSpice_running`).

17. `bool start_continuous_transient(double step, double window, int64_t sleep_ms = 25)`
- Validates arguments (`step > 0`, `window > step`).
- Stops prior continuous thread if active.
- Starts worker loop that repeatedly:
  - runs one transient chunk
  - reads all vectors
  - annotates frame with chunk metadata
  - optionally appends CSV rows
  - emits `continuous_transient_frame`
  - advances next start time by `window`
- Emits start/stop lifecycle signals.

18. `void stop_continuous_transient()`
- Public stop wrapper for continuous worker.

19. `bool is_continuous_transient_running() const`
- Returns continuous worker running state.

20. `Dictionary get_continuous_transient_state() const`
- Returns current loop params/state (`running`, `step`, `window`, `next_start`, `sleep_ms`).

### Data retrieval

21. `Array get_voltage(const String &node_name)`
- Queries vector `v(node_name)` and returns samples.

22. `Array get_current(const String &source_name)`
- Queries vector `i(source_name)` and returns samples.

23. `Array get_time_vector()`
- Returns `time` vector samples.

24. `Dictionary get_all_vectors()`
- Enumerates all vectors in current plot and returns each as an array.

25. `PackedStringArray get_all_vector_names()`
- Returns vector names only for current plot.

### External control API (`external` workflows)

26. `void set_voltage_source(const String &source_name, double voltage)`
- Alias convenience method; internally delegates to `set_external_value`.

27. `double get_voltage_source(const String &source_name)`
- Alias convenience method; internally delegates to `get_external_value`.

28. `void set_external_value(const String &name, double value)`
- Stores one external source value in thread-safe map.
- Read later by ngspice sync callback when source value is requested.

29. `double get_external_value(const String &name)`
- Retrieves current stored external value (default `0.0` if missing).

30. `void set_external_values(const Dictionary &values)`
- Bulk update for multiple external names in one call.

31. `void set_switch_state(const String &name, bool closed)`
- Sets external control to `1.0` (closed) or `0.0` (open).
- Useful when a switch/control source is modeled as binary external input.

### Continuous CSV export

32. `bool configure_continuous_csv_export(const String &csv_path, const PackedStringArray &signals = PackedStringArray())`
- Opens CSV file (truncate/create) and writes header: `time,signal,value`.
- Optional `signals` filter limits exported vectors.
- Empty filter exports all array vectors from each frame (except metadata keys).

33. `void disable_continuous_csv_export()`
- Disables export, clears filters/path state, closes stream.

34. `bool is_continuous_csv_export_enabled() const`
- Returns enabled flag.

35. `String get_continuous_csv_export_path() const`
- Returns active CSV output path.

## Private/Internal Functions in Depth

### File-local helper functions (anonymous namespace)

1. `to_lower_copy`:
- Lowercases text for case-insensitive parsing.

2. `trim_copy`:
- Trims ASCII whitespace from both ends.

3. `starts_with_ci`:
- Case-insensitive prefix matching.

4. `unquote_copy`:
- Removes wrapping double quotes if present.

5. `maybe_quote`:
- Adds quotes when original token was quoted.

6. `replace_all_copy`:
- Replaces all occurrences of substring.

7. `expand_pdk_root`:
- Expands `$PDK_ROOT` and `${PDK_ROOT}` from arg/env.

8. `resolve_path_token`:
- Converts path token to absolute normalized path with base dir + PDK expansion.

9. `read_file_lines`:
- Reads file as text lines and strips trailing `\r`.

10. `to_logical_lines`:
- Folds SPICE continuation lines starting with `+`.

11. `append_unique`:
- Adds signal names once while preserving discovery order.

12. `parse_wrdata_signals`:
- Parses `.control` `wrdata` lines to extract vector names for generated `.save`.

13. `rewrite_include_or_lib`:
- Rewrites `.include`/`.lib` paths to absolute resolved paths.

14. `rewrite_input_file_path`:
- Rewrites `input_file="..."` path values to absolute resolved paths.

### ngspice callback functions

1. `ng_send_char`:
- Receives ngspice console output.
- Emits Godot signal `ngspice_output` and prints log line.

2. `ng_send_stat`:
- Status callback placeholder (currently no-op).

3. `ng_controlled_exit`:
- Controlled-exit callback (logs request).

4. `ng_send_data`:
- Streams per-sample vector values while simulation runs.
- Emits `simulation_data_ready` signal dictionary.

5. `ng_send_init_data`:
- Receives vector metadata once simulation initializes.

6. `ng_bg_thread_running`:
- Emits `simulation_started` / `simulation_finished` based on background state.

7. `ng_get_vsrc_data`:
- Provides external value for voltage source callback requests.

8. `ng_get_isrc_data`:
- Provides external value for current source callback requests.

### Class-private methods

1. `_bind_methods`:
- Registers all methods/signals with Godot ClassDB.

2. `load_ngspice_library`:
- Dynamically loads library and function pointers (platform-specific candidates).

3. `unload_ngspice_library`:
- Closes dynamic library handle.

4. `run_transient_chunk`:
- Executes one `tran` command safely under mutex.

5. `append_csv_rows`:
- Converts one continuous frame dictionary into `time,signal,value` rows.
- Prevents duplicate timestamps using `csv_last_export_time`.

6. `stop_continuous_thread`:
- Requests worker stop and joins thread.

## Threading and Command Safety

- ngspice command calls are serialized with `ng_command_mutex`.
- external control dictionary is protected with `voltage_sources_mutex`.
- CSV stream writes are protected with `csv_mutex`.
- continuous mode runs in a worker thread; UI should consume emitted signals on the main thread.

## Inverter Runtime Step-Through (Simple Example)

This walkthrough shows how to run an inverter continuously, modulate input via `external`, and export animation data to CSV.

### 1. Example inverter deck (external-driven input)

```spice
* inverter_external.spice
VDD vdd 0 1.8
VIN in  0 0 external

MP out in vdd vdd PMOS L=180n W=1u
MN out in 0   0   NMOS L=180n W=500n
CLOAD out 0 5f

.model NMOS NMOS LEVEL=1 VTO=0.45 KP=200u LAMBDA=0.05
.model PMOS PMOS LEVEL=1 VTO=-0.45 KP=80u  LAMBDA=0.05

.tran 50p 50n
.control
  wrdata inv.csv time v(in) v(out)
.endc
.end
```

Notes:
- Key point is `external` on `VIN` so runtime controls come from `set_external_value("vin", value)`.
- Source name used by callback must match what ngspice requests for that source in your deck.

### 2. Startup sequence (GDScript)

```gdscript
if sim.initialize_ngspice():
    var result = sim.run_spice_file("/absolute/path/inverter_external.spice", "")
    var csv_ok = sim.configure_continuous_csv_export(
        ProjectSettings.globalize_path("user://uploads/inverter_trace.csv"),
        PackedStringArray(["v(in)", "v(out)"])
    )
    if csv_ok:
        sim.start_continuous_transient(5e-11, 2e-9, 25)
```

### 3. Runtime modulation loop

```gdscript
var t := 0.0
func _process(delta: float) -> void:
    t += delta
    var vin := 0.9 + 0.9 * sin(2.0 * PI * 1.0e6 * t)  # 0..1.8 V sine
    sim.set_external_value("vin", vin)
```

### 4. Continuous frame behavior

Each continuous cycle:
1. runs `tran step chunk_stop chunk_start`
2. reads vectors (`time`, `v(in)`, `v(out)`, ...)
3. emits `continuous_transient_frame`
4. appends new timestamps to CSV
5. increments chunk window

### 5. Stop/cleanup

```gdscript
sim.stop_continuous_transient()
sim.disable_continuous_csv_export()
sim.shutdown_ngspice()
```

## Example CSV Output

Generated format is row-oriented:

```csv
time,signal,value
0.0000000000000000,v(in),0
0.0000000000000000,v(out),1.8
0.0000000000500000,v(in),0.12
0.0000000000500000,v(out),1.79
0.0000000001000000,v(in),0.24
0.0000000001000000,v(out),1.73
0.0000000001500000,v(in),0.36
0.0000000001500000,v(out),1.58
0.0000000002000000,v(in),0.48
0.0000000002000000,v(out),1.31
```

Interpretation:
- each timestamp can have multiple rows (one row per signal)
- values are exported in discovery/filter order
- timestamps are monotonic and deduplicated across continuous chunks

## Practical Guidance

- For smooth animation, keep `window` moderately larger than `step` (for example 20x to 200x).
- For interactive controls, prefer `set_external_values({...})` for bulk updates each frame.
- Use signal filters in CSV export to keep files manageable (`v(in)`, `v(out)`, key branch currents only).
- If you need open/close switch behavior, map switch control source to `set_switch_state(name, closed)`.
