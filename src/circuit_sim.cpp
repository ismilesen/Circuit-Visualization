#include "circuit_sim.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <iomanip>
#include <limits>
#include <thread>
#include <chrono>

using namespace godot;
namespace fs = std::filesystem;

namespace {
// Returns a lowercase copy for case-insensitive matching.
std::string to_lower_copy(const std::string &input) {
    std::string out = input;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

// Trims leading/trailing ASCII whitespace.
std::string trim_copy(const std::string &input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        start++;
    }

    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        end--;
    }

    return input.substr(start, end - start);
}

// Case-insensitive prefix check.
bool starts_with_ci(const std::string &line, const std::string &prefix) {
    if (line.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(line[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

// Removes wrapping double quotes when present.
std::string unquote_copy(const std::string &value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

// Adds wrapping double quotes when requested.
std::string maybe_quote(const std::string &value, bool should_quote) {
    return should_quote ? "\"" + value + "\"" : value;
}

// Replaces all occurrences of a token in a copied string.
std::string replace_all_copy(std::string value, const std::string &needle, const std::string &replacement) {
    size_t pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos) {
        value.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return value;
}

// Expands PDK_ROOT references from argument or environment.
std::string expand_pdk_root(std::string value, const std::string &pdk_root) {
    const char *env_pdk_root = std::getenv("PDK_ROOT");
    const std::string root = pdk_root.empty() ? (env_pdk_root ? std::string(env_pdk_root) : std::string()) : pdk_root;
    if (root.empty()) {
        return value;
    }
    value = replace_all_copy(value, "$PDK_ROOT", root);
    value = replace_all_copy(value, "${PDK_ROOT}", root);
    return value;
}

// Resolves a path token to an absolute normalized path.
std::string resolve_path_token(const std::string &raw_path, const fs::path &base_dir, const std::string &pdk_root) {
    std::string expanded = expand_pdk_root(raw_path, pdk_root);
    if (expanded.empty()) {
        return expanded;
    }

    fs::path p(expanded);
    if (p.is_relative()) {
        p = fs::absolute(base_dir / p);
    } else {
        p = fs::absolute(p);
    }
    return p.lexically_normal().string();
}

// Reads a text file line-by-line with CRLF cleanup.
bool read_file_lines(const fs::path &file_path, std::vector<std::string> &lines_out) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines_out.push_back(line);
    }
    return true;
}

// Folds SPICE continuation lines that start with '+'.
std::vector<std::string> to_logical_lines(const std::vector<std::string> &physical_lines) {
    std::vector<std::string> logical_lines;
    for (const std::string &raw : physical_lines) {
        std::string trimmed = trim_copy(raw);
        if (!trimmed.empty() && trimmed.front() == '+' && !logical_lines.empty()) {
            logical_lines.back() += " " + trim_copy(trimmed.substr(1));
        } else {
            logical_lines.push_back(raw);
        }
    }
    return logical_lines;
}

// Tracks signal names once, preserving first-seen ordering.
void append_unique(std::vector<std::string> &signals, std::unordered_set<std::string> &seen, const std::string &signal) {
    if (signal.empty()) {
        return;
    }
    std::string key = to_lower_copy(signal);
    if (seen.insert(key).second) {
        signals.push_back(signal);
    }
}

// Extracts wrdata probe names used to build a .save directive.
void parse_wrdata_signals(const std::string &line, std::vector<std::string> &signals, std::unordered_set<std::string> &seen) {
    std::istringstream iss(line);
    std::string token;
    bool found_wrdata = false;
    int token_index_after_wrdata = 0;

    while (iss >> token) {
        std::string lower = to_lower_copy(token);
        if (!found_wrdata) {
            if (lower == "wrdata") {
                found_wrdata = true;
            }
            continue;
        }

        token_index_after_wrdata++;
        if (token_index_after_wrdata == 1) {
            // First token after wrdata is output file path.
            continue;
        }

        if (starts_with_ci(token, "v(") || starts_with_ci(token, "i(") || lower == "time") {
            append_unique(signals, seen, token);
        }
    }
}

// Rewrites .include/.lib paths to absolute paths, with optional PDK expansion.
std::string rewrite_include_or_lib(const std::string &line, const fs::path &base_dir, const std::string &pdk_root) {
    std::string trimmed = trim_copy(line);
    std::string lower = to_lower_copy(trimmed);
    bool is_include = starts_with_ci(lower, ".include");
    bool is_lib = starts_with_ci(lower, ".lib");
    if (!is_include && !is_lib) {
        return line;
    }

    std::istringstream iss(trimmed);
    std::string directive;
    std::string path_token;
    iss >> directive >> path_token;
    if (path_token.empty()) {
        return line;
    }

    bool was_quoted = (path_token.size() >= 2 && path_token.front() == '"' && path_token.back() == '"');
    std::string resolved = resolve_path_token(unquote_copy(path_token), base_dir, pdk_root);

    std::string rebuilt = directive + " " + maybe_quote(resolved, was_quoted);

    if (is_lib) {
        std::string section;
        if (iss >> section) {
            rebuilt += " " + section;
        }
    }
    return rebuilt;
}

// Rewrites input_file="..." paths to absolute normalized paths.
std::string rewrite_input_file_path(const std::string &line, const fs::path &base_dir, const std::string &pdk_root) {
    const std::string key = "input_file=\"";
    size_t start = line.find(key);
    if (start == std::string::npos) {
        return line;
    }

    size_t value_start = start + key.size();
    size_t value_end = line.find('"', value_start);
    if (value_end == std::string::npos) {
        return line;
    }

    std::string path_value = line.substr(value_start, value_end - value_start);
    std::string resolved = resolve_path_token(path_value, base_dir, pdk_root);
    return line.substr(0, value_start) + resolved + line.substr(value_end);
}

} // namespace

// Static instance for callbacks
CircuitSimulator* CircuitSimulator::instance = nullptr;

// Callback functions for ngspice
// Streams ngspice console output to Godot and an exposed signal.
static int ng_send_char(char *output, int id, void *user_data) {
    (void)id;
    (void)user_data;
    if (CircuitSimulator::instance) {
        CircuitSimulator::instance->emit_signal("ngspice_output", String(output));
    }
    UtilityFunctions::print(String("[ngspice] ") + String(output));
    return 0;
}

// Receives status updates from ngspice (currently ignored).
static int ng_send_stat(char *status, int id, void *user_data) {
    (void)status;
    (void)id;
    (void)user_data;
    // Status updates during simulation
    return 0;
}

// Handles ngspice shutdown callback notifications.
static int ng_controlled_exit(int status, bool immediate, bool exit_on_quit, int id, void *user_data) {
    (void)status;
    (void)immediate;
    (void)exit_on_quit;
    (void)id;
    (void)user_data;
    UtilityFunctions::print("ngspice exit requested");
    return 0;
}

// Publishes streamed simulation samples while ngspice runs.
static int ng_send_data(pvecvaluesall data, int count, int id, void *user_data) {
    (void)count;
    (void)id;
    (void)user_data;
    // Called during simulation with new data points
    if (CircuitSimulator::instance) {
        Dictionary dict;
        for (int i = 0; i < data->veccount; i++) {
            pvecvalues vec = data->vecsa[i];
            dict[String(vec->name)] = vec->creal;
        }
        CircuitSimulator::instance->emit_signal("simulation_data_ready", dict);
    }
    return 0;
}

// Receives vector metadata once a simulation is initialized.
static int ng_send_init_data(pvecinfoall data, int id, void *user_data) {
    (void)id;
    (void)user_data;
    // Called before simulation with vector info
    UtilityFunctions::print(String("Simulation initialized with ") + String::num_int64(data->veccount) + " vectors");
    return 0;
}

// Emits lifecycle signals when the ngspice background thread starts/stops.
static int ng_bg_thread_running(bool running, int id, void *user_data) {
    (void)id;
    (void)user_data;
    if (CircuitSimulator::instance) {
        if (running) {
            CircuitSimulator::instance->emit_signal("simulation_started");
        } else {
            CircuitSimulator::instance->emit_signal("simulation_finished");
        }
    }
    return 0;
}

// Callback for interactive voltage source control
// Supplies interactive voltage source values requested by ngspice.
static int ng_get_vsrc_data(double *voltage, double time, char *node_name, int id, void *user_data) {
    (void)time;
    (void)id;
    (void)user_data;
    if (CircuitSimulator::instance) {
        *voltage = CircuitSimulator::instance->get_external_value(String(node_name));
    }
    return 0;
}

// Callback for interactive current source control.
static int ng_get_isrc_data(double *current, double time, char *node_name, int id, void *user_data) {
    (void)time;
    (void)id;
    (void)user_data;
    if (CircuitSimulator::instance) {
        *current = CircuitSimulator::instance->get_external_value(String(node_name));
    }
    return 0;
}

// Registers methods and signals exposed to GDScript.
void CircuitSimulator::_bind_methods() {
    // Initialization methods
    ClassDB::bind_method(D_METHOD("initialize_ngspice"), &CircuitSimulator::initialize_ngspice);
    ClassDB::bind_method(D_METHOD("shutdown_ngspice"), &CircuitSimulator::shutdown_ngspice);
    ClassDB::bind_method(D_METHOD("is_initialized"), &CircuitSimulator::is_initialized);

    // Circuit loading
    ClassDB::bind_method(D_METHOD("load_netlist", "netlist_path"), &CircuitSimulator::load_netlist);
    ClassDB::bind_method(D_METHOD("load_netlist_string", "netlist_content"), &CircuitSimulator::load_netlist_string);
    ClassDB::bind_method(D_METHOD("run_spice_file", "spice_path", "pdk_root"), &CircuitSimulator::run_spice_file, DEFVAL(""));
    ClassDB::bind_method(D_METHOD("get_current_netlist"), &CircuitSimulator::get_current_netlist);

    // Simulation control
    ClassDB::bind_method(D_METHOD("run_simulation"), &CircuitSimulator::run_simulation);
    ClassDB::bind_method(D_METHOD("run_transient", "step", "stop", "start"), &CircuitSimulator::run_transient, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("run_dc", "source", "start", "stop", "step"), &CircuitSimulator::run_dc);
    ClassDB::bind_method(D_METHOD("pause_simulation"), &CircuitSimulator::pause_simulation);
    ClassDB::bind_method(D_METHOD("resume_simulation"), &CircuitSimulator::resume_simulation);
    ClassDB::bind_method(D_METHOD("stop_simulation"), &CircuitSimulator::stop_simulation);
    ClassDB::bind_method(D_METHOD("is_running"), &CircuitSimulator::is_running);
    ClassDB::bind_method(
        D_METHOD("start_continuous_transient", "step", "window", "sleep_ms"),
        &CircuitSimulator::start_continuous_transient,
        DEFVAL(int64_t(25))
    );
    ClassDB::bind_method(D_METHOD("stop_continuous_transient"), &CircuitSimulator::stop_continuous_transient);
    ClassDB::bind_method(D_METHOD("is_continuous_transient_running"), &CircuitSimulator::is_continuous_transient_running);
    ClassDB::bind_method(D_METHOD("get_continuous_transient_state"), &CircuitSimulator::get_continuous_transient_state);

    // Data retrieval
    ClassDB::bind_method(D_METHOD("get_voltage", "node_name"), &CircuitSimulator::get_voltage);
    ClassDB::bind_method(D_METHOD("get_current", "source_name"), &CircuitSimulator::get_current);
    ClassDB::bind_method(D_METHOD("get_time_vector"), &CircuitSimulator::get_time_vector);
    ClassDB::bind_method(D_METHOD("get_all_vectors"), &CircuitSimulator::get_all_vectors);
    ClassDB::bind_method(D_METHOD("get_all_vector_names"), &CircuitSimulator::get_all_vector_names);

    // Interactive control
    ClassDB::bind_method(D_METHOD("set_voltage_source", "source_name", "voltage"), &CircuitSimulator::set_voltage_source);
    ClassDB::bind_method(D_METHOD("get_voltage_source", "source_name"), &CircuitSimulator::get_voltage_source);
    ClassDB::bind_method(D_METHOD("set_external_value", "name", "value"), &CircuitSimulator::set_external_value);
    ClassDB::bind_method(D_METHOD("get_external_value", "name"), &CircuitSimulator::get_external_value);
    ClassDB::bind_method(D_METHOD("set_external_values", "values"), &CircuitSimulator::set_external_values);
    ClassDB::bind_method(D_METHOD("set_switch_state", "name", "closed"), &CircuitSimulator::set_switch_state);
    ClassDB::bind_method(
        D_METHOD("configure_continuous_csv_export", "csv_path", "signals"),
        &CircuitSimulator::configure_continuous_csv_export,
        DEFVAL(PackedStringArray())
    );
    ClassDB::bind_method(D_METHOD("disable_continuous_csv_export"), &CircuitSimulator::disable_continuous_csv_export);
    ClassDB::bind_method(D_METHOD("is_continuous_csv_export_enabled"), &CircuitSimulator::is_continuous_csv_export_enabled);
    ClassDB::bind_method(D_METHOD("get_continuous_csv_export_path"), &CircuitSimulator::get_continuous_csv_export_path);

    // Signals
    ADD_SIGNAL(MethodInfo("simulation_started"));
    ADD_SIGNAL(MethodInfo("simulation_finished"));
    ADD_SIGNAL(MethodInfo("simulation_data_ready", PropertyInfo(Variant::DICTIONARY, "data")));
    ADD_SIGNAL(MethodInfo("ngspice_output", PropertyInfo(Variant::STRING, "message")));
    ADD_SIGNAL(MethodInfo("continuous_transient_started"));
    ADD_SIGNAL(MethodInfo("continuous_transient_stopped"));
    ADD_SIGNAL(MethodInfo("continuous_transient_frame", PropertyInfo(Variant::DICTIONARY, "frame")));
    ADD_SIGNAL(MethodInfo("continuous_csv_export_error", PropertyInfo(Variant::STRING, "message")));
}

// Initializes simulator state and ngspice function pointers.
CircuitSimulator::CircuitSimulator() {
    initialized = false;
    current_netlist = "";
    ngspice_handle = nullptr;
    ng_Init = nullptr;
    ng_Init_Sync = nullptr;
    ng_Command = nullptr;
    ng_GetVecInfo = nullptr;
    ng_CurPlot = nullptr;
    ng_AllVecs = nullptr;
    ng_Circ = nullptr;
    ng_Running = nullptr;
    continuous_stop_requested = false;
    continuous_running = false;
    continuous_step = 0.0;
    continuous_window = 0.0;
    continuous_next_start.store(0.0);
    continuous_sleep_ms = 25;
    csv_export_enabled = false;
    csv_export_path = "";
    csv_last_export_time = -std::numeric_limits<double>::infinity();
    instance = this;
}

// Stops worker threads and releases ngspice resources.
CircuitSimulator::~CircuitSimulator() {
    stop_continuous_thread();
    disable_continuous_csv_export();
    if (initialized) {
        shutdown_ngspice();
    }
    if (instance == this) {
        instance = nullptr;
    }
}

// Dynamically loads the ngspice shared library and required symbols.
bool CircuitSimulator::load_ngspice_library() {
#ifdef _WIN32
    ngspice_handle = LoadLibraryA("ngspice.dll");
    if (!ngspice_handle) {
        // Try loading from bin folder
        ngspice_handle = LoadLibraryA("bin/ngspice.dll");
    }
    if (!ngspice_handle) {
        UtilityFunctions::printerr("Failed to load ngspice.dll");
        return false;
    }

    ng_Init = (int (*)(SendChar*, SendStat*, ControlledExit*, SendData*, SendInitData*, BGThreadRunning*, void*))
        GetProcAddress(ngspice_handle, "ngSpice_Init");
    ng_Init_Sync = (int (*)(GetVSRCData*, GetISRCData*, GetSyncData*, int*, void*))
        GetProcAddress(ngspice_handle, "ngSpice_Init_Sync");
    ng_Command = (int (*)(char*))
        GetProcAddress(ngspice_handle, "ngSpice_Command");
    ng_GetVecInfo = (pvector_info (*)(char*))
        GetProcAddress(ngspice_handle, "ngGet_Vec_Info");
    ng_CurPlot = (char* (*)())
        GetProcAddress(ngspice_handle, "ngSpice_CurPlot");
    ng_AllVecs = (char** (*)(char*))
        GetProcAddress(ngspice_handle, "ngSpice_AllVecs");
    ng_Circ = (int (*)(char**))
        GetProcAddress(ngspice_handle, "ngSpice_Circ");
    ng_Running = (bool (*)())
        GetProcAddress(ngspice_handle, "ngSpice_running");
#else
    std::vector<std::string> candidates;
#ifdef __APPLE__
    candidates = {
        "libngspice.dylib",
        "./libngspice.dylib",
        "./bin/libngspice.dylib",
        "./project/bin/libngspice.dylib",
        "./ngspice/libngspice.dylib",
        "/opt/homebrew/lib/libngspice.dylib",
        "/usr/local/lib/libngspice.dylib",
        "libngspice.so",
        "./libngspice.so",
        "./bin/libngspice.so",
        "./project/bin/libngspice.so",
        "/opt/homebrew/lib/libngspice.so",
        "/usr/local/lib/libngspice.so"
    };
#else
    candidates = {
        "libngspice.so",
        "./libngspice.so",
        "./bin/libngspice.so",
        "./project/bin/libngspice.so",
        "/usr/lib/libngspice.so",
        "/usr/local/lib/libngspice.so"
    };
#endif

    String attempted_paths;
    String last_error;
    for (const std::string &candidate : candidates) {
        ngspice_handle = dlopen(candidate.c_str(), RTLD_NOW);
        if (ngspice_handle) {
            UtilityFunctions::print("Loaded ngspice library from: " + String(candidate.c_str()));
            break;
        }
        if (!attempted_paths.is_empty()) {
            attempted_paths += ", ";
        }
        attempted_paths += String(candidate.c_str());
        const char *err = dlerror();
        if (err) {
            last_error = String(err);
        }
    }

    if (!ngspice_handle) {
        UtilityFunctions::printerr("Failed to load ngspice library. Tried: " + attempted_paths);
        if (!last_error.is_empty()) {
            UtilityFunctions::printerr("Last dlopen error: " + last_error);
        }
        return false;
    }

    ng_Init = (int (*)(SendChar*, SendStat*, ControlledExit*, SendData*, SendInitData*, BGThreadRunning*, void*))
        dlsym(ngspice_handle, "ngSpice_Init");
    ng_Init_Sync = (int (*)(GetVSRCData*, GetISRCData*, GetSyncData*, int*, void*))
        dlsym(ngspice_handle, "ngSpice_Init_Sync");
    ng_Command = (int (*)(char*))
        dlsym(ngspice_handle, "ngSpice_Command");
    ng_GetVecInfo = (pvector_info (*)(char*))
        dlsym(ngspice_handle, "ngGet_Vec_Info");
    ng_CurPlot = (char* (*)())
        dlsym(ngspice_handle, "ngSpice_CurPlot");
    ng_AllVecs = (char** (*)(char*))
        dlsym(ngspice_handle, "ngSpice_AllVecs");
    ng_Circ = (int (*)(char**))
        dlsym(ngspice_handle, "ngSpice_Circ");
    ng_Running = (bool (*)())
        dlsym(ngspice_handle, "ngSpice_running");
#endif

    if (!ng_Init || !ng_Command) {
        UtilityFunctions::printerr("Failed to load required ngspice functions");
        unload_ngspice_library();
        return false;
    }

    return true;
}

// Releases the loaded ngspice shared library handle.
void CircuitSimulator::unload_ngspice_library() {
#ifdef _WIN32
    if (ngspice_handle) {
        FreeLibrary(ngspice_handle);
        ngspice_handle = nullptr;
    }
#else
    if (ngspice_handle) {
        dlclose(ngspice_handle);
        ngspice_handle = nullptr;
    }
#endif
}

// Initializes ngspice and wires callback hooks.
bool CircuitSimulator::initialize_ngspice() {
    if (initialized) {
        UtilityFunctions::print("ngspice already initialized");
        return true;
    }

    if (!load_ngspice_library()) {
        return false;
    }

    int ret = ng_Init(
        ng_send_char,
        ng_send_stat,
        ng_controlled_exit,
        ng_send_data,
        ng_send_init_data,
        ng_bg_thread_running,
        this
    );

    if (ret != 0) {
        UtilityFunctions::printerr("ngSpice_Init failed with code: " + String::num_int64(ret));
        unload_ngspice_library();
        return false;
    }

    // Set up voltage source callback for interactive control
    if (ng_Init_Sync) {
        ng_Init_Sync(ng_get_vsrc_data, ng_get_isrc_data, nullptr, nullptr, this);
    }

    initialized = true;
    UtilityFunctions::print("ngspice initialized successfully");
    return true;
}

// Stops activity and tears down embedded ngspice safely.
void CircuitSimulator::shutdown_ngspice() {
    stop_continuous_thread();
    disable_continuous_csv_export();

    if (!initialized) {
        return;
    }

    if (ng_Command) {
        // In embedded mode, quit may crash on some macOS/libngspice builds during teardown.
        // Halt background execution and reset state instead of invoking com_quit.
        ng_Command((char*)"bg_halt");
        if (ng_Running) {
            for (int i = 0; i < 50 && ng_Running(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        ng_Command((char*)"reset");
    }

    unload_ngspice_library();
    initialized = false;
    UtilityFunctions::print("ngspice shut down");
}

// Reports whether ngspice is ready for commands.
bool CircuitSimulator::is_initialized() const {
    return initialized;
}

// Loads a netlist file directly into ngspice.
bool CircuitSimulator::load_netlist(const String &netlist_path) {
    if (!initialized) {
        UtilityFunctions::printerr("ngspice not initialized");
        return false;
    }

    CharString path_utf8 = netlist_path.utf8();
    std::string cmd = "source " + std::string(path_utf8.get_data());
    int ret = ng_Command((char*)cmd.c_str());

    if (ret != 0) {
        UtilityFunctions::printerr("Failed to load netlist: " + netlist_path);
        return false;
    }

    current_netlist = netlist_path;
    UtilityFunctions::print("Loaded netlist: " + netlist_path);
    return true;
}

// Loads an in-memory netlist string using ngSpice_Circ.
bool CircuitSimulator::load_netlist_string(const String &netlist_content) {
    if (!initialized) {
        UtilityFunctions::printerr("ngspice not initialized");
        return false;
    }

    if (!ng_Circ) {
        UtilityFunctions::printerr("ngSpice_Circ not available");
        return false;
    }

    // Split netlist into lines
    PackedStringArray lines = netlist_content.split("\n");
    std::vector<char*> circ_lines;
    std::vector<std::string> line_storage;
    line_storage.reserve(lines.size());
    circ_lines.reserve(lines.size() + 1);

    for (int i = 0; i < lines.size(); i++) {
        line_storage.push_back(std::string(lines[i].utf8().get_data()));
    }
    for (std::string &line : line_storage) {
        circ_lines.push_back(const_cast<char *>(line.c_str()));
    }
    circ_lines.push_back(nullptr);  // Null terminator

    int ret = ng_Circ(circ_lines.data());

    if (ret != 0) {
        UtilityFunctions::printerr("Failed to load netlist from string");
        return false;
    }

    current_netlist = netlist_content;
    UtilityFunctions::print("Loaded netlist from string");
    return true;
}

// Normalizes and runs a SPICE deck, returning sampled vectors and metadata.
Dictionary CircuitSimulator::run_spice_file(const String &spice_path, const String &pdk_root) {
    Dictionary result;

    if (!initialized) {
        UtilityFunctions::printerr("ngspice not initialized");
        return result;
    }

    if (!ng_Circ || !ng_Command || !ng_GetVecInfo) {
        UtilityFunctions::printerr("Missing ngspice API functions required for run_spice_file");
        return result;
    }

    CharString spice_utf8 = spice_path.utf8();
    fs::path spice_fs_path = fs::absolute(fs::path(spice_utf8.get_data())).lexically_normal();
    fs::path base_dir = spice_fs_path.parent_path();
    std::string pdk_root_str = std::string(pdk_root.utf8().get_data());

    std::vector<std::string> physical_lines;
    if (!read_file_lines(spice_fs_path, physical_lines)) {
        UtilityFunctions::printerr(String("Failed to read .spice file: ") + String(spice_fs_path.string().c_str()));
        return result;
    }

    std::vector<std::string> logical_lines = to_logical_lines(physical_lines);
    std::vector<std::string> normalized_lines;
    std::vector<std::string> save_signals;
    std::unordered_set<std::string> seen_signals;

    bool inside_control = false;
    bool has_end = false;
    bool has_tran = false;
    std::string extracted_tran;

    for (const std::string &original_line : logical_lines) {
        std::string trimmed = trim_copy(original_line);
        std::string lower = to_lower_copy(trimmed);

        if (starts_with_ci(trimmed, ".control")) {
            inside_control = true;
            continue;
        }
        if (inside_control) {
            if (starts_with_ci(trimmed, ".endc")) {
                inside_control = false;
                continue;
            }

            if (extracted_tran.empty() && (starts_with_ci(trimmed, "tran ") || starts_with_ci(trimmed, ".tran "))) {
                extracted_tran = starts_with_ci(trimmed, ".tran ") ? trimmed : ".tran " + trimmed.substr(5);
            }
            if (lower.find("wrdata") != std::string::npos) {
                parse_wrdata_signals(trimmed, save_signals, seen_signals);
            }
            continue;
        }

        std::string rewritten = rewrite_include_or_lib(original_line, base_dir, pdk_root_str);
        rewritten = rewrite_input_file_path(rewritten, base_dir, pdk_root_str);

        std::string rewritten_trimmed = trim_copy(rewritten);
        std::string rewritten_lower = to_lower_copy(rewritten_trimmed);
        if (starts_with_ci(rewritten_trimmed, ".tran ")) {
            has_tran = true;
        }
        if (rewritten_lower == ".end") {
            has_end = true;
        }
        normalized_lines.push_back(rewritten);
    }

    if (!has_tran && !extracted_tran.empty()) {
        normalized_lines.push_back(extracted_tran);
    }

    if (!save_signals.empty()) {
        std::string save_line = ".save time";
        for (const std::string &signal : save_signals) {
            if (to_lower_copy(signal) == "time") {
                continue;
            }
            save_line += " " + signal;
        }
        normalized_lines.push_back(save_line);
    }

    if (!has_end) {
        normalized_lines.push_back(".end");
    }

    std::vector<char *> circ_lines;
    circ_lines.reserve(normalized_lines.size() + 1);
    for (std::string &line : normalized_lines) {
        circ_lines.push_back(const_cast<char *>(line.c_str()));
    }
    circ_lines.push_back(nullptr);

    int load_ret = ng_Circ(circ_lines.data());
    if (load_ret != 0) {
        UtilityFunctions::printerr("ngSpice_Circ failed while loading normalized .spice lines");
        return result;
    }

    int run_ret = ng_Command((char *)"run");
    if (run_ret != 0) {
        UtilityFunctions::printerr("ngSpice_Command(\"run\") failed");
        return result;
    }

    Array time_data;
    pvector_info time_vec = ng_GetVecInfo((char *)"time");
    if (time_vec && time_vec->v_realdata) {
        for (int i = 0; i < time_vec->v_length; i++) {
            time_data.append(time_vec->v_realdata[i]);
        }
        result["time"] = time_data;
    }

    for (const std::string &signal : save_signals) {
        if (to_lower_copy(signal) == "time") {
            continue;
        }
        pvector_info vec = ng_GetVecInfo((char *)signal.c_str());
        if (!vec || !vec->v_realdata) {
            continue;
        }
        Array data;
        for (int i = 0; i < vec->v_length; i++) {
            data.append(vec->v_realdata[i]);
        }
        result[String(signal.c_str())] = data;
    }

    std::ostringstream netlist_stream;
    for (const std::string &line : normalized_lines) {
        netlist_stream << line << "\n";
    }
    current_netlist = String(netlist_stream.str().c_str());
    result["normalized_netlist"] = current_netlist;
    result["signal_count"] = static_cast<int64_t>(save_signals.size());

    return result;
}

// Returns the most recently loaded/normalized netlist text.
String CircuitSimulator::get_current_netlist() const {
    return current_netlist;
}

// Starts a background ngspice run (bg_run).
bool CircuitSimulator::run_simulation() {
    if (!initialized) {
        UtilityFunctions::printerr("ngspice not initialized");
        return false;
    }

    std::lock_guard<std::mutex> lock(ng_command_mutex);
    int ret = ng_Command((char*)"bg_run");
    return ret == 0;
}

// Executes one transient command chunk with explicit start/stop bounds.
bool CircuitSimulator::run_transient_chunk(double step, double stop, double start) {
    if (!initialized || !ng_Command) {
        return false;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "tran %g %g %g", step, stop, start);

    std::lock_guard<std::mutex> lock(ng_command_mutex);
    int ret = ng_Command(cmd);
    return ret == 0;
}

// Runs a transient analysis command.
bool CircuitSimulator::run_transient(double step, double stop, double start) {
    if (!initialized) {
        UtilityFunctions::printerr("ngspice not initialized");
        return false;
    }

    return run_transient_chunk(step, stop, start);
}

// Runs a DC sweep command.
bool CircuitSimulator::run_dc(const String &source, double start, double stop, double step) {
    if (!initialized) {
        UtilityFunctions::printerr("ngspice not initialized");
        return false;
    }

    CharString source_utf8 = source.utf8();
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "dc %s %g %g %g", source_utf8.get_data(), start, stop, step);
    std::lock_guard<std::mutex> lock(ng_command_mutex);
    int ret = ng_Command(cmd);

    return ret == 0;
}

// Pauses a running background simulation.
void CircuitSimulator::pause_simulation() {
    if (!initialized) {
        return;
    }
    std::lock_guard<std::mutex> lock(ng_command_mutex);
    ng_Command((char*)"bg_halt");
}

// Resumes a paused background simulation.
void CircuitSimulator::resume_simulation() {
    if (!initialized) {
        return;
    }
    std::lock_guard<std::mutex> lock(ng_command_mutex);
    ng_Command((char*)"bg_resume");
}

// Halts current simulation activity.
void CircuitSimulator::stop_simulation() {
    if (!initialized) {
        return;
    }

    std::lock_guard<std::mutex> lock(ng_command_mutex);
    ng_Command((char*)"bg_halt");
    UtilityFunctions::print("Simulation stopped");
}

// Queries ngspice for current background run state.
bool CircuitSimulator::is_running() const {
    if (!initialized || !ng_Running) {
        return false;
    }
    return ng_Running();
}

// Starts a looping transient stream that emits frame snapshots.
bool CircuitSimulator::start_continuous_transient(double step, double window, int64_t sleep_ms) {
    if (!initialized || !ng_Command) {
        UtilityFunctions::printerr("ngspice not initialized");
        return false;
    }
    if (step <= 0.0 || window <= 0.0) {
        UtilityFunctions::printerr("start_continuous_transient requires positive step and window");
        return false;
    }
    if (window <= step) {
        UtilityFunctions::printerr("start_continuous_transient requires window > step");
        return false;
    }

    stop_continuous_thread();

    continuous_step = step;
    continuous_window = window;
    continuous_next_start.store(0.0);
    continuous_sleep_ms = sleep_ms < 1 ? 1 : sleep_ms;
    continuous_stop_requested = false;
    continuous_running = true;

    emit_signal("continuous_transient_started");

    continuous_thread = std::thread([this]() {
        while (!continuous_stop_requested.load()) {
            const double chunk_start = continuous_next_start.load();
            const double chunk_stop = chunk_start + continuous_window;

            if (!run_transient_chunk(continuous_step, chunk_stop, chunk_start)) {
                UtilityFunctions::printerr("Continuous transient chunk failed");
                break;
            }

            Dictionary frame = get_all_vectors();
            frame["chunk_start"] = chunk_start;
            frame["chunk_stop"] = chunk_stop;
            frame["step"] = continuous_step;
            if (!append_csv_rows(frame)) {
                emit_signal("continuous_csv_export_error", "Failed to append CSV rows");
                break;
            }
            emit_signal("continuous_transient_frame", frame);

            continuous_next_start.store(chunk_stop);
            std::this_thread::sleep_for(std::chrono::milliseconds(continuous_sleep_ms));
        }

        continuous_running = false;
        emit_signal("continuous_transient_stopped");
    });

    return true;
}

// Exports one rolling frame to CSV as (time, signal, value) rows.
bool CircuitSimulator::append_csv_rows(const Dictionary &vectors) {
    std::lock_guard<std::mutex> lock(csv_mutex);
    if (!csv_export_enabled || !csv_stream.is_open()) {
        return true;
    }
    if (!vectors.has("time")) {
        return true;
    }

    Array time_values = vectors["time"];
    if (time_values.is_empty()) {
        return true;
    }

    PackedStringArray signal_names;
    if (!csv_signal_filter.is_empty()) {
        signal_names = csv_signal_filter;
    } else {
        Array keys = vectors.keys();
        for (int i = 0; i < keys.size(); i++) {
            String key = keys[i];
            if (key == "time" || key == "chunk_start" || key == "chunk_stop" || key == "step") {
                continue;
            }
            Variant value = vectors[key];
            if (value.get_type() == Variant::ARRAY) {
                signal_names.append(key);
            }
        }
    }

    for (int i = 0; i < time_values.size(); i++) {
        const double t = static_cast<double>(time_values[i]);
        if (t <= csv_last_export_time) {
            continue;
        }

        for (int j = 0; j < signal_names.size(); j++) {
            const String &signal = signal_names[j];
            if (!vectors.has(signal)) {
                continue;
            }

            Array signal_values = vectors[signal];
            if (i >= signal_values.size()) {
                continue;
            }

            Variant sample = signal_values[i];
            if (sample.get_type() != Variant::FLOAT && sample.get_type() != Variant::INT) {
                continue;
            }

            csv_stream << std::setprecision(16) << t
                       << "," << std::string(signal.utf8().get_data())
                       << "," << static_cast<double>(sample)
                       << "\n";
        }

        csv_last_export_time = t;
    }

    if (!csv_stream.good()) {
        csv_export_enabled = false;
        return false;
    }

    csv_stream.flush();
    return true;
}

// Signals and joins the continuous worker thread.
void CircuitSimulator::stop_continuous_thread() {
    continuous_stop_requested = true;
    if (continuous_thread.joinable()) {
        continuous_thread.join();
    }
    continuous_running = false;
}

// Public wrapper to stop continuous transient streaming.
void CircuitSimulator::stop_continuous_transient() {
    stop_continuous_thread();
}

// Reports whether continuous transient mode is active.
bool CircuitSimulator::is_continuous_transient_running() const {
    return continuous_running.load();
}

// Returns current continuous transient loop parameters/state.
Dictionary CircuitSimulator::get_continuous_transient_state() const {
    Dictionary state;
    state["running"] = continuous_running.load();
    state["step"] = continuous_step;
    state["window"] = continuous_window;
    state["next_start"] = continuous_next_start.load();
    state["sleep_ms"] = continuous_sleep_ms;
    return state;
}

// Fetches a node voltage vector as v(node_name).
Array CircuitSimulator::get_voltage(const String &node_name) {
    Array result;

    if (!initialized || !ng_GetVecInfo) {
        return result;
    }

    CharString name_utf8 = (String("v(") + node_name + ")").utf8();
    pvector_info vec = ng_GetVecInfo((char*)name_utf8.get_data());

    if (vec && vec->v_realdata) {
        for (int i = 0; i < vec->v_length; i++) {
            result.append(vec->v_realdata[i]);
        }
    }

    return result;
}

// Fetches a source current vector as i(source_name).
Array CircuitSimulator::get_current(const String &source_name) {
    Array result;

    if (!initialized || !ng_GetVecInfo) {
        return result;
    }

    CharString name_utf8 = (String("i(") + source_name + ")").utf8();
    pvector_info vec = ng_GetVecInfo((char*)name_utf8.get_data());

    if (vec && vec->v_realdata) {
        for (int i = 0; i < vec->v_length; i++) {
            result.append(vec->v_realdata[i]);
        }
    }

    return result;
}

// Fetches the current time vector.
Array CircuitSimulator::get_time_vector() {
    Array result;

    if (!initialized || !ng_GetVecInfo) {
        return result;
    }

    pvector_info vec = ng_GetVecInfo((char*)"time");

    if (vec && vec->v_realdata) {
        for (int i = 0; i < vec->v_length; i++) {
            result.append(vec->v_realdata[i]);
        }
    }

    return result;
}

// Fetches all vectors from the active ngspice plot.
Dictionary CircuitSimulator::get_all_vectors() {
    Dictionary result;

    if (!initialized || !ng_CurPlot || !ng_AllVecs || !ng_GetVecInfo) {
        return result;
    }

    char* cur_plot = ng_CurPlot();
    if (!cur_plot) {
        return result;
    }

    char** all_vecs = ng_AllVecs(cur_plot);
    if (!all_vecs) {
        return result;
    }

    for (int i = 0; all_vecs[i] != nullptr; i++) {
        pvector_info vec = ng_GetVecInfo(all_vecs[i]);
        if (vec && vec->v_realdata) {
            Array data;
            for (int j = 0; j < vec->v_length; j++) {
                data.append(vec->v_realdata[j]);
            }
            result[String(all_vecs[i])] = data;
        }
    }

    return result;
}

// Returns only vector names from the active ngspice plot.
PackedStringArray CircuitSimulator::get_all_vector_names() {
    PackedStringArray result;

    if (!initialized || !ng_CurPlot || !ng_AllVecs) {
        return result;
    }

    char* cur_plot = ng_CurPlot();
    if (!cur_plot) {
        return result;
    }

    char** all_vecs = ng_AllVecs(cur_plot);
    if (!all_vecs) {
        return result;
    }

    for (int i = 0; all_vecs[i] != nullptr; i++) {
        result.append(String(all_vecs[i]));
    }

    return result;
}

// Sets an interactive voltage source override value.
void CircuitSimulator::set_voltage_source(const String &source_name, double voltage) {
    set_external_value(source_name, voltage);
    UtilityFunctions::print("Set " + source_name + " to " + String::num(voltage) + " (external)");
}

// Returns the latest interactive voltage source value.
double CircuitSimulator::get_voltage_source(const String &source_name) {
    return get_external_value(source_name);
}

// Sets a named external source value for ngspice sync callbacks.
void CircuitSimulator::set_external_value(const String &name, double value) {
    std::lock_guard<std::mutex> lock(voltage_sources_mutex);
    voltage_sources[name] = value;
}

// Returns the latest named external source value.
double CircuitSimulator::get_external_value(const String &name) {
    std::lock_guard<std::mutex> lock(voltage_sources_mutex);
    if (voltage_sources.has(name)) {
        return static_cast<double>(voltage_sources[name]);
    }
    return 0.0;
}

// Bulk update for external values to reduce script call overhead.
void CircuitSimulator::set_external_values(const Dictionary &values) {
    std::lock_guard<std::mutex> lock(voltage_sources_mutex);
    Array keys = values.keys();
    for (int i = 0; i < keys.size(); i++) {
        String key = keys[i];
        Variant value = values[key];
        if (value.get_type() == Variant::FLOAT || value.get_type() == Variant::INT) {
            voltage_sources[key] = static_cast<double>(value);
        }
    }
}

// Helper for switch controls that map to binary external values.
void CircuitSimulator::set_switch_state(const String &name, bool closed) {
    set_external_value(name, closed ? 1.0 : 0.0);
}

// Configures CSV export path and optional vector filter for continuous mode.
bool CircuitSimulator::configure_continuous_csv_export(const String &csv_path, const PackedStringArray &signals) {
    if (csv_path.is_empty()) {
        return false;
    }

    CharString path_utf8 = csv_path.utf8();
    fs::path out_path(path_utf8.get_data());
    out_path = fs::absolute(out_path).lexically_normal();

    std::error_code ec;
    fs::create_directories(out_path.parent_path(), ec);

    std::lock_guard<std::mutex> lock(csv_mutex);
    if (csv_stream.is_open()) {
        csv_stream.close();
    }

    csv_stream.open(out_path, std::ios::out | std::ios::trunc);
    if (!csv_stream.is_open()) {
        csv_export_enabled = false;
        return false;
    }

    csv_stream << "time,signal,value\n";
    csv_stream.flush();
    csv_export_enabled = true;
    csv_export_path = String(out_path.string().c_str());
    csv_signal_filter = signals;
    csv_last_export_time = -std::numeric_limits<double>::infinity();
    return true;
}

// Stops CSV export and closes the file handle.
void CircuitSimulator::disable_continuous_csv_export() {
    std::lock_guard<std::mutex> lock(csv_mutex);
    csv_export_enabled = false;
    csv_signal_filter.clear();
    csv_export_path = "";
    csv_last_export_time = -std::numeric_limits<double>::infinity();
    if (csv_stream.is_open()) {
        csv_stream.close();
    }
}

// Returns whether continuous CSV export is currently active.
bool CircuitSimulator::is_continuous_csv_export_enabled() const {
    return csv_export_enabled;
}

// Returns the active CSV export file path.
String CircuitSimulator::get_continuous_csv_export_path() const {
    return csv_export_path;
}
