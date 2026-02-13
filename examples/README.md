# SPICE Examples

This folder contains simple netlists for testing upload + simulation in the Godot app.

## Files

- `inv.spice`
  - CMOS inverter transient test.
  - Input: pulse on `v(in)`.
  - Expect: `v(out)` is inverted and delayed relative to `v(in)`.

- `inv_original.spice`
  - Original inverter deck moved from `/Users/sebastianavila/School/CSV/inv.spice`.
  - Includes a `.control` block with `run` and `quit`.
  - Useful for regression checks against embedded ngspice control handling.

- `ring_osc_3stage.spice`
  - 3-stage ring oscillator with behavioral inverters.
  - Nodes: `n1`, `n2`, `n3`.
  - Expect: sustained oscillation with phase shift between nodes.

- `relaxation_osc.spice`
  - RC relaxation oscillator with ideal comparator behavior.
  - Nodes: `out`, `cap`.
  - Expect: `v(out)` square-like toggling and `v(cap)` charge/discharge ramp.

- `rlc_ringdown.spice`
  - Damped RLC transient response.
  - Node/current: `v(n2)`, `i(L1)`.
  - Expect: decaying oscillation (ringdown) after stimulus edge.

- `rc_lowpass_step.spice`
  - RC low-pass baseline (non-oscillator).
  - Nodes: `v(in)`, `v(out)`.
  - Expect: exponential rise/fall at `v(out)` with RC delay.

## Quick Usage

1. Open the app and click **Upload Files...**.
2. Select one `.spice` file from this folder.
3. Run simulation.
4. Verify expected waveform behavior above.

## Notes

- These decks are self-contained and do not require PDK model includes.
- Most examples include `.control` with `wrdata ...` for CSV-style output vectors.
- If one deck fails, test `rc_lowpass_step.spice` first to confirm the basic pipeline is healthy.
