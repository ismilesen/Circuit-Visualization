# Inverter Control Meeting Script

Use this script for a 10-15 minute walkthrough of the current inverter control workflow.

## 1) Opening (1 minute)

Today I’ll show the current ngspice background integration with a stable control path in Godot:
- play/pause simulation
- toggle inverter input (`VIN`)
- change supply (`VDD`)
- clean shutdown without crash on quit

## 2) Scope and current UI (1-2 minutes)

Scene:
- `project/circuit_simulator.tscn`

UI script:
- `project/inverter_ui.gd`

Current controls:
- Play/Pause
- `VDD` slider
- `VIN` High/Neutral toggle

Note: waveform graph/readout are intentionally removed for now while we prioritize a reliable control backend.

## 3) Backend flow (3 minutes)

Main runtime script:
- `project/circuit_simulator.gd`

Flow:
1. Initialize ngspice (`initialize_ngspice()`).
2. Load generated inverter deck (`load_inverter_demo(...)`).
3. Start background run (`run_simulation()`).
4. On parameter changes:
   - update local `_params`
   - stop current run
   - regenerate/reload netlist
   - restart background run

This design replaces fragile `alterparam` behavior in this embedded context.

## 4) Netlist model and controls (2 minutes)

Netlist builder:
- `src/circuit_sim.cpp` (`build_inverter_netlist`)

Current control parameters:
- `VDD` from slider
- `VIN_LEVEL` from toggle

Input source:
- `VIN in 0 {VIN_LEVEL}`

So the toggle switches between:
- neutral = `0V`
- high = `VDD`

## 5) Stability fix highlight (2 minutes)

Two key fixes:
1. Safe netlist-string loading in `load_netlist_string(...)` to avoid pointer invalidation.
2. Safe ngspice shutdown:
   - avoid `quit` command path
   - use `bg_halt` + wait + `reset` + unload

Result:
- no crash reported on stop/quit in latest test cycle.

## 6) Live demo steps (3-4 minutes)

1. Build extension:
```bash
scons -Q
```

2. Run project scene (`project/circuit_simulator.tscn`).

3. Interact with controls:
- toggle `VIN` and confirm ngspice log updates
- move `VDD` slider and confirm simulation reload/rerun
- pause/resume simulation

4. Stop run / close scene and confirm no teardown crash.

## 7) Close (30 seconds)

Main outcome: the interactive control loop is stable and working end-to-end. We now have a solid base to reintroduce visualization with lower risk.
