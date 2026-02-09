# Inverter Control Pipeline Overview

This document summarizes the current inverter demo pipeline after the UI/control and stability updates.

## Goal

Run ngspice in the background from Godot, allow user interaction (`VIN` toggle + `VDD` slider), and keep teardown stable on scene/app exit.

## Current UI

Scene:
- `project/circuit_simulator.tscn`

Script:
- `project/inverter_ui.gd`

Controls:
- Play/Pause button (`pause_animation()` / `play_animation()`)
- `VDD` slider (`set_vdd(...)`)
- `VIN` toggle (`set_input_high(...)`)

The graph/readout widgets were intentionally removed for now to focus on robust backend control.

## Runtime flow

Main script:
- `project/circuit_simulator.gd`

Startup:
1. Connect ngspice-related signals.
2. `initialize_ngspice()`
3. `start_inverter_demo()`:
   - `load_inverter_demo(_params)`
   - `run_simulation()` (background)

User parameter update:
1. UI updates call `set_vdd(...)` or `set_input_high(...)`.
2. `update_parameters(...)` updates `_params`.
3. `_reload_and_run_with_params()`:
   - stop current background run if needed
   - clear local waveform buffers
   - regenerate inverter netlist from params
   - reload netlist
   - rerun background simulation

This avoids unreliable in-place `alterparam` usage for this embedded flow.

## Inverter netlist behavior

Netlist generation:
- `src/circuit_sim.cpp` (`build_inverter_netlist(...)`)

Important params:
- `VDD`
- `VIN_LEVEL` (0V or `VDD`, controlled by UI toggle)
- device/load/transient params (`WN`, `WP`, `LCH`, `CLOAD`, `tstep`, `tstop`)

Input source is currently:
- `VIN in 0 {VIN_LEVEL}`

## Stability fixes applied

### 1) Netlist string loading safety

In `load_netlist_string(...)`, line storage and pointer arrays are now reserved and built in stable order before calling `ngSpice_Circ(...)`. This prevents pointer invalidation and corrupted line reads.

### 2) Teardown crash mitigation

In `shutdown_ngspice()`:
- Removed `ngSpice_Command("quit")` path.
- Uses `bg_halt`, waits for background stop, then `reset`, then unloads library.

This change was made due to a reproducible macOS abort in libngspice during `com_quit`.

## Current status

- UI controls are successfully connected to backend parameter updates.
- `VIN` and `VDD` updates are reflected in ngspice logs.
- No crashes reported on stop/quit after shutdown changes.

## Next optional step

If/when needed, reintroduce plotting with a separate, explicit data retrieval path (post-run vector pull or controlled streaming callback mode), keeping shutdown behavior unchanged.
