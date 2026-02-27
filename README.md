
# USING THE CIRCUITSIMULATOR NODE


## GDSCRIPT EXAMPLE:

```cpp
    extends CircuitSimulator

    func _ready():
        # Initialize ngspice
        if initialize_ngspice():
            print("ngspice ready!")

        # Load and normalize a SPICE deck
        var run_result: Dictionary = load_netlist("res://circuits/example.spice", "")
        print(run_result.keys())

        # Start callback-driven continuous streaming
        configure_continuous_memory_buffer(PackedStringArray(["time", "v(out)"]), 10000)
        start_continuous_transient(1e-11, 1e6, 25)
```

## BUFFER CONSUMPTION EXAMPLE (INDEX MAPPING + POP):

Each buffered row is a PackedFloat64Array. The column order matches
get_continuous_memory_signal_names().

```cpp
    extends Node

    @onready var sim: CircuitSimulator = $CircuitSimulator

    var idx_time: int = -1
    var idx_vout: int = -1

    func _ready() -> void:
        if not sim.initialize_ngspice():
            push_error("Failed to init ngspice")
            return

        var load_result: Dictionary = sim.load_netlist("res://circuits/example.spice", "")
        if load_result.is_empty():
            push_error("load_netlist failed")
            return

        sim.configure_continuous_memory_buffer(
            PackedStringArray(["time", "v(out)"]),
            10000
        )
        sim.start_continuous_transient(1e-11, 1e6, 25)

        # Signal names are provided by callback init; map once after startup.
        await get_tree().process_frame
        var names: PackedStringArray = sim.get_continuous_memory_signal_names()
        idx_time = names.find("time")
        idx_vout = names.find("v(out)")
        if idx_time < 0 or idx_vout < 0:
            push_error("Required signals not found in memory buffer names: %s" % [str(names)])

    func _process(_delta: float) -> void:
        # Pop only new samples since last frame.
        var rows: Array = sim.pop_continuous_memory_samples(256)
        for row_variant in rows:
            var row: PackedFloat64Array = row_variant
            if idx_time < 0 or idx_vout < 0:
                continue
            if row.size() <= maxi(idx_time, idx_vout):
                continue

            var t: float = row[idx_time]
            var vout: float = row[idx_vout]
            _apply_animation_from_voltage(t, vout) # example function
```

## AVAILABLE METHODS:
```
    initialize_ngspice()              - Initialize the simulator (call first)
    load_netlist(path, pdk_root)      - Normalize/load a SPICE file
    start_continuous_transient(...)   - Start callback-driven transient streaming
    stop_continuous_transient()       - Stop continuous streaming
    is_continuous_transient_running() - Check continuous run state
    configure_continuous_memory_buffer(signals, max_samples) - Enable RAM buffer
    clear_continuous_memory_buffer()  - Clear buffered samples
    get_continuous_memory_signal_names() - Column names for buffered samples
    get_continuous_memory_snapshot()  - Read buffered rows (non-destructive)
    pop_continuous_memory_samples(n)  - Read and consume oldest rows
    get_continuous_memory_sample_count() - Current buffered row count
```


## SIGNALS:
```
    simulation_started                - Emitted when simulation begins
    simulation_finished               - Emitted when simulation completes
    simulation_data_ready(data)       - Emitted with data during simulation
    ngspice_output(message)           - Console output from ngspice
```