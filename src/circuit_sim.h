#ifndef CIRCUIT_SIM_H
#define CIRCUIT_SIM_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "sharedspice.h"

namespace godot {

class CircuitSimulator : public Node {
    GDCLASS(CircuitSimulator, Node)

private:
    bool initialized;
    String current_netlist;

    // Dynamic library handle
#ifdef _WIN32
    HMODULE ngspice_handle;
#else
    void* ngspice_handle;
#endif

    // Function pointers for ngspice API
    int (*ng_Init)(SendChar*, SendStat*, ControlledExit*, SendData*, SendInitData*, BGThreadRunning*, void*);
    int (*ng_Init_Sync)(GetVSRCData*, GetISRCData*, GetSyncData*, int*, void*);
    int (*ng_Command)(char*);
    pvector_info (*ng_GetVecInfo)(char*);
    char* (*ng_CurPlot)();
    char** (*ng_AllVecs)(char*);
    int (*ng_Circ)(char**);
    bool (*ng_Running)();

    // Load ngspice dynamically
    bool load_ngspice_library();
    void unload_ngspice_library();
    bool run_transient_chunk(double step, double stop, double start);

    // Voltage source values for interactive control
    Dictionary voltage_sources;
    std::mutex voltage_sources_mutex;
    std::mutex ng_command_mutex;

    // Continuous transient loop state.
    std::thread continuous_thread;
    std::atomic<bool> continuous_stop_requested;
    std::atomic<bool> continuous_running;
    double continuous_step;
    double continuous_window;
    std::atomic<double> continuous_next_start;
    int64_t continuous_sleep_ms;
    void stop_continuous_thread();
    bool append_csv_rows(const Dictionary &vectors);

    // Optional CSV export for continuous transient snapshots.
    std::ofstream csv_stream;
    std::atomic<bool> csv_export_enabled;
    String csv_export_path;
    PackedStringArray csv_signal_filter;
    double csv_last_export_time;
    std::mutex csv_mutex;

protected:
    static void _bind_methods();

public:
    CircuitSimulator();
    ~CircuitSimulator();

    // Initialization
    bool initialize_ngspice();
    void shutdown_ngspice();
    bool is_initialized() const;

    // Circuit loading
    bool load_netlist(const String &netlist_path);
    bool load_netlist_string(const String &netlist_content);
    Dictionary run_spice_file(const String &spice_path, const String &pdk_root = "");
    String get_current_netlist() const;

    // Simulation control
    bool run_simulation();
    bool run_transient(double step, double stop, double start = 0.0);
    bool run_dc(const String &source, double start, double stop, double step);
    void pause_simulation();
    void resume_simulation();
    void stop_simulation();
    bool is_running() const;
    bool start_continuous_transient(double step, double window, int64_t sleep_ms = 25);
    void stop_continuous_transient();
    bool is_continuous_transient_running() const;
    Dictionary get_continuous_transient_state() const;

    // Data retrieval
    Array get_voltage(const String &node_name);
    Array get_current(const String &source_name);
    Array get_time_vector();
    Dictionary get_all_vectors();
    PackedStringArray get_all_vector_names();

    // Interactive control (for switches)
    void set_voltage_source(const String &source_name, double voltage);
    double get_voltage_source(const String &source_name);
    void set_external_value(const String &name, double value);
    double get_external_value(const String &name);
    void set_external_values(const Dictionary &values);
    void set_switch_state(const String &name, bool closed);

    bool configure_continuous_csv_export(const String &csv_path, const PackedStringArray &signals = PackedStringArray());
    void disable_continuous_csv_export();
    bool is_continuous_csv_export_enabled() const;
    String get_continuous_csv_export_path() const;

    // Static instance for callbacks
    static CircuitSimulator* instance;
};

} // namespace godot

#endif // CIRCUIT_SIM_H
