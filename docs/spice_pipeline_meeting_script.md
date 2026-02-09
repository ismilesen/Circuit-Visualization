# SPICE Pipeline Meeting Script

Use this script to explain and demo the new `.spice` pipeline in 10-15 minutes.

## 1) Opening (1 minute)

Today I’ll show how we now ingest uploaded `.spice` files directly into ngspice with in-memory normalization, then run and query vectors in one path.

The new goals were:
- avoid brittle shell-based loading
- normalize paths and control blocks consistently
- get deterministic vector outputs for plotting/export

## 2) What changed (2 minutes)

We added two public methods on `CircuitSimulator`:
- `run_spice_file(spice_path, pdk_root = "")`
- `test_spice_pipeline(output_dir = "")`

`run_spice_file` is the production path.
`test_spice_pipeline` is a built-in smoke test that validates the full flow end-to-end.

Reference:
- `src/circuit_sim.h`
- `src/circuit_sim.cpp`

## 3) Pipeline walkthrough (3 minutes)

`run_spice_file` performs this sequence:
1. Read physical lines from uploaded deck.
2. Build logical lines (join continuation lines starting with `+`).
3. Normalize in memory:
- rewrite `.include` / `.lib` paths to absolute
- expand `$PDK_ROOT` / `${PDK_ROOT}`
- rewrite `input_file="..."` to absolute
- strip `.control ... .endc`
- extract `tran` and `wrdata` vectors from control block
4. Append generated directives:
- `.tran ...` if missing in top-level deck
- `.save time ...` using extracted vectors
- `.end` if missing
5. Convert to `char**` + trailing `NULL`.
6. `ngSpice_Circ(lines)`
7. `ngSpice_Command("run")`
8. Query vectors (`time`, `v(...)`, etc.) via `ngGet_Vec_Info`

## 4) What we return (1 minute)

Result dictionary includes:
- `time`
- extracted vectors (for example `v(in)`, `v(out)`)
- `normalized_netlist` (exact text sent to ngspice)
- `signal_count`

This gives us both data and auditability.

## 5) Reliability/test story (2 minutes)

`test_spice_pipeline` writes a synthetic deck that intentionally includes:
- `$PDK_ROOT` include
- `+` continuation
- `.control` block with `tran` + `wrdata`
- missing `.end`

Then it runs `run_spice_file` and checks:
- vectors exist and are non-empty
- vector lengths are consistent
- control block removed
- `.tran`, `.save`, `.end` are present in normalized deck
- include path was rewritten to absolute

It returns:
- `passed`
- `checks` dictionary
- `errors` list

## 6) macOS fix callout (1 minute)

We also fixed library loading on macOS.
Before: it tried only `libngspice.so`.
Now: loader checks `.dylib` candidates first (including `project/bin` and `ngspice`), then `.so` fallback.

## 7) Live demo steps (3-4 minutes)

1. Rebuild extension:
```bash
scons -Q
```

2. In `project/circuit_simulator.gd`, run:
```gdscript
func _ready():
	if !initialize_ngspice():
		push_error("init failed")
		return

	var report = test_spice_pipeline()
	print("passed: ", report.get("passed", false))
	print("checks: ", report.get("checks", {}))
	print("errors: ", report.get("errors", []))
```

3. Show:
- `passed = true`
- check matrix
- generated normalized netlist

4. Optional real-deck run:
```gdscript
var result = run_spice_file("/absolute/path/to/deck.spice", "/absolute/path/to/pdk_root")
print(result.keys())
```

## 8) Close (30 seconds)

Main outcome: we now have a robust, testable SPICE ingestion path that is portable across local environments, easier to debug, and ready for frontend plotting/export workflows.
