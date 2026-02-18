Run ngspice in rolling windows
Instead of one long run, repeatedly run short .tran windows (for example 0.1-1 ms each).
After each window, fetch latest vectors, append to a ring buffer, then start next window.
This gives “continuous” behavior and lets you change inputs/parameters live.
Drive visuals from buffered/interpolated data
In Godot _process(delta), interpolate between sampled points in your ring buffer.
Render at frame rate (60 FPS) even if ngspice updates slower (for example 10-30 Hz).
This makes motion look continuous even when simulation is discrete.
Separate simulation clock from render clock
Keep simulation updates on a timer (Timer node or worker loop).
Keep visual updates in _process.
Never block frame rendering waiting for ngspice.
For modulation, update external sources between chunks
Use external source methods (set_voltage_source/set_external_value/set_external_values) before each next chunk.
This enables LFO-like modulation, envelope control, and interactive knobs.
Add fallback “fake continuous” mode
If ngspice can’t keep up, loop the most recent stable waveform and blend into fresh data when it arrives.
