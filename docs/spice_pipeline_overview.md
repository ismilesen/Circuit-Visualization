# SPICE Pipeline Overview

This document explains the new SPICE ingestion and simulation pipeline added to `CircuitSimulator`.

## Why this was added

The goal is to load uploaded `.spice` decks directly into ngspice using `ngSpice_Circ()` instead of shell `source`, while normalizing the deck in memory so it is portable and predictable.

## New public methods

## `run_spice_file(spice_path, pdk_root = "")`

Location:
- `src/circuit_sim.h`
- `src/circuit_sim.cpp`

What it does:
1. Reads the `.spice` file from disk.
2. Builds logical lines by joining continuation lines starting with `+`.
3. Rewrites lines in memory:
- rewrites `.include` and `.lib` paths to absolute paths
- expands `$PDK_ROOT` / `${PDK_ROOT}` (from argument or environment)
- rewrites `input_file="..."` paths to absolute
4. Removes `.control ... .endc` block from the deck and extracts:
- `tran` command (if present)
- `wrdata` vectors (for `.save`)
5. Appends generated directives:
- `.tran ...` if top-level `.tran` is missing and one was extracted
- `.save time ...` from extracted `wrdata` signals
- `.end` if missing
6. Converts normalized lines to `char**` + trailing `NULL`.
7. Calls:
- `ngSpice_Circ(lines)`
- `ngSpice_Command("run")`
8. Queries vectors with `ngGet_Vec_Info` and returns a `Dictionary`.

Return payload:
- `time`: time vector array (if available)
- extracted vectors like `v(out)`, `v(in)`, etc. (if available)
- `normalized_netlist`: final netlist text that was sent to ngspice
- `signal_count`: number of extracted save signals

## `test_spice_pipeline(output_dir = "")`

Location:
- `src/circuit_sim.h`
- `src/circuit_sim.cpp`

What it does:
1. Creates a temporary test run directory.
2. Writes a tiny include file and a synthetic `.spice` deck.
3. Deck intentionally includes:
- `.include "$PDK_ROOT/..."`
- line continuation with `+`
- `.control/.endc` with `tran` and `wrdata`
- missing `.end`
4. Runs `run_spice_file(...)` on that deck.
5. Evaluates checks and returns a report dictionary.

Report fields:
- `passed`: overall pass/fail
- `checks`: per-condition booleans
- `errors`: human-readable failed checks
- `test_spice_path`, `test_include_path`
- `normalized_netlist`

Main checks:
- vectors exist: `time`, `v(in)`, `v(out)`
- vectors are non-empty and same length
- `.control/.endc` removed
- `.tran` appended
- `.save` appended from `wrdata`
- `.end` present
- include path rewritten to absolute

## Internal helper functions (new, file-local)

In `src/circuit_sim.cpp` under anonymous namespace:
- string utilities: lowercase/trim/prefix checks
- path expansion and normalization:
- `expand_pdk_root(...)`
- `resolve_path_token(...)`
- file/line handling:
- `read_file_lines(...)`
- `to_logical_lines(...)`
- control parsing:
- `parse_wrdata_signals(...)`
- line rewrites:
- `rewrite_include_or_lib(...)`
- `rewrite_input_file_path(...)`

These are private implementation details (not exposed in header).

## ngspice loading improvement (macOS fix)

`load_ngspice_library()` was updated to probe multiple candidate library paths and extensions:
- `.dylib` paths first on macOS
- `.so` fallbacks for compatibility

This fixed startup failures where only `libngspice.so` was being attempted on macOS.

## Typical call flow from GDScript

```gdscript
if initialize_ngspice():
	var report = test_spice_pipeline()
	print(report)

	var result = run_spice_file("/absolute/path/to/deck.spice", "/absolute/path/to/pdk_root")
	print(result.keys())
```

## Known limitations

- `run_spice_file` currently focuses on transient-style control extraction (`tran` + `wrdata` vectors).
- `wrdata` parsing assumes first token after `wrdata` is output filename, remaining vector-like tokens are saved.
- Advanced control scripts beyond this pattern are intentionally stripped.
