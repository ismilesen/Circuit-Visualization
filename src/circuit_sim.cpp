#include "circuit_sim.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
#include <unordered_set>
#include <vector>
#include <iomanip>
#include <thread>
#include <chrono>

using namespace godot;
namespace fs = std::filesystem;

namespace {
std::string to_lower_copy(const std::string &input) {
    std::string out = input;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

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

std::string unquote_copy(const std::string &value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string maybe_quote(const std::string &value, bool should_quote) {
    return should_quote ? "\"" + value + "\"" : value;
}

std::string replace_all_copy(std::string value, const std::string &needle, const std::string &replacement) {
    size_t pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos) {
        value.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return value;
}

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

void append_unique(std::vector<std::string> &signals, std::unordered_set<std::string> &seen, const std::string &signal) {
    if (signal.empty()) {
        return;
    }
    std::string key = to_lower_copy(signal);
    if (seen.insert(key).second) {
        signals.push_back(signal);
    }
}

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

std::string format_double(double value) {
    std::ostringstream ss;
    ss << std::setprecision(12) << value;
    return ss.str();
}
} // namespace

// Static instance for callbacks
CircuitSimulator* CircuitSimulator::instance = nullptr;

// Callback functions for ngspice
static int ng_send_char(char *output, int id, void *user_data) {
    if (CircuitSimulator::instance) {
        CircuitSimulator::instance->emit_signal("ngspice_output", String(output));
    }
    UtilityFunctions::print(String("[ngspice] ") + String(output));
    return 0;
}

static int ng_send_stat(char *status, int id, void *user_data) {
    // Status updates during simulation
    return 0;
}

static int ng_controlled_exit(int status, bool immediate, bool exit_on_quit, int id, void *user_data) {
    UtilityFunctions::print("ngspice exit requested");
    return 0;
}

static int ng_send_data(pvecvaluesall data, int count, int id, void *user_data) {
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

static int ng_send_init_data(pvecinfoall data, int id, void *user_data) {
    // Called before simulation with vector info
    UtilityFunctions::print(String("Simulation initialized with ") + String::num_int64(data->veccount) + " vectors");
    return 0;
}

static int ng_bg_thread_running(bool running, int id, void *user_data) {
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
static int ng_get_vsrc_data(double *voltage, double time, char *node_name, int id, void *user_data) {
    if (CircuitSimulator::instance) {
        *voltage = CircuitSimulator::instance->get_voltage_source(String(node_name));
    }
    return 0;
}

void CircuitSimulator::_bind_methods() {
    // Initialization methods
    ClassDB::bind_method(D_METHOD("initialize_ngspice"), &CircuitSimulator::initialize_ngspice);
    ClassDB::bind_method(D_METHOD("shutdown_ngspice"), &CircuitSimulator::shutdown_ngspice);
    ClassDB::bind_method(D_METHOD("is_initialized"), &CircuitSimulator::is_initialized);

    // Circuit loading
    ClassDB::bind_method(D_METHOD("load_netlist", "netlist_path"), &CircuitSimulator::load_netlist);
    ClassDB::bind_method(D_METHOD("load_netlist_string", "netlist_content"), &CircuitSimulator::load_netlist_string);
    ClassDB::bind_method(D_METHOD("run_spice_file", "spice_path", "pdk_root"), &CircuitSimulator::run_spice_file, DEFVAL(""));
    ClassDB::bind_method(D_METHOD("test_spice_pipeline", "output_dir"), &CircuitSimulator::test_spice_pipeline, DEFVAL(""));
    ClassDB::bind_method(D_METHOD("get_current_netlist"), &CircuitSimulator::get_current_netlist);

    // Simulation control
    ClassDB::bind_method(D_METHOD("run_simulation"), &CircuitSimulator::run_simulation);
    ClassDB::bind_method(D_METHOD("run_transient", "step", "stop", "start"), &CircuitSimulator::run_transient, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("run_dc", "source", "start", "stop", "step"), &CircuitSimulator::run_dc);
    ClassDB::bind_method(D_METHOD("pause_simulation"), &CircuitSimulator::pause_simulation);
    ClassDB::bind_method(D_METHOD("resume_simulation"), &CircuitSimulator::resume_simulation);
    ClassDB::bind_method(D_METHOD("stop_simulation"), &CircuitSimulator::stop_simulation);
    ClassDB::bind_method(D_METHOD("is_running"), &CircuitSimulator::is_running);

    // Data retrieval
    ClassDB::bind_method(D_METHOD("get_voltage", "node_name"), &CircuitSimulator::get_voltage);
    ClassDB::bind_method(D_METHOD("get_current", "source_name"), &CircuitSimulator::get_current);
    ClassDB::bind_method(D_METHOD("get_time_vector"), &CircuitSimulator::get_time_vector);
    ClassDB::bind_method(D_METHOD("get_all_vectors"), &CircuitSimulator::get_all_vectors);
    ClassDB::bind_method(D_METHOD("get_all_vector_names"), &CircuitSimulator::get_all_vector_names);

    // Interactive control
    ClassDB::bind_method(D_METHOD("set_voltage_source", "source_name", "voltage"), &CircuitSimulator::set_voltage_source);
    ClassDB::bind_method(D_METHOD("get_voltage_source", "source_name"), &CircuitSimulator::get_voltage_source);
    ClassDB::bind_method(D_METHOD("set_parameter", "name", "value"), &CircuitSimulator::set_parameter);

    // Signals
    ADD_SIGNAL(MethodInfo("simulation_started"));
    ADD_SIGNAL(MethodInfo("simulation_finished"));
    ADD_SIGNAL(MethodInfo("simulation_data_ready", PropertyInfo(Variant::DICTIONARY, "data")));
    ADD_SIGNAL(MethodInfo("ngspice_output", PropertyInfo(Variant::STRING, "message")));
}

CircuitSimulator::CircuitSimulator() {
    initialized = false;
    current_netlist = "";
    ngspice_handle = nullptr;
    ng_Init = nullptr;
    ng_Init_Sync = nullptr;
    ng_Command = nullptr;
    ng_GetVecInfo = nullptr;
    ng_CurPlot = nullptr;
    ng_AllPlots = nullptr;
    ng_AllVecs = nullptr;
    ng_Circ = nullptr;
    ng_Running = nullptr;
    instance = this;
}

CircuitSimulator::~CircuitSimulator() {
    if (initialized) {
        shutdown_ngspice();
    }
    if (instance == this) {
        instance = nullptr;
    }
}

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
    ng_AllPlots = (char** (*)())
        GetProcAddress(ngspice_handle, "ngSpice_AllPlots");
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
        "libngspice.so",
        "./libngspice.so",
        "./bin/libngspice.so",
        "./project/bin/libngspice.so"
    };
#else
    candidates = {
        "libngspice.so",
        "./libngspice.so",
        "./bin/libngspice.so",
        "./project/bin/libngspice.so"
    };
#endif

    String attempted_paths;
    String last_error;
    for (const std::string &candidate : candidates) {
        ngspice_handle = dlopen(candidate.c_str(), RTLD_NOW);
        if (ngspice_handle) {
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
    ng_AllPlots = (char** (*)())
        dlsym(ngspice_handle, "ngSpice_AllPlots");
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
        ng_Init_Sync(ng_get_vsrc_data, nullptr, nullptr, nullptr, this);
    }

    initialized = true;
    UtilityFunctions::print("ngspice initialized successfully");
    return true;
}

void CircuitSimulator::shutdown_ngspice() {
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

bool CircuitSimulator::is_initialized() const {
    return initialized;
}

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

Dictionary CircuitSimulator::test_spice_pipeline(const String &output_dir) {
    Dictionary report;
    Array errors;
    Dictionary checks;

    auto fail = [&](const String &msg) {
        errors.append(msg);
    };

    if (!initialized) {
        fail("ngspice not initialized");
        report["passed"] = false;
        report["errors"] = errors;
        report["checks"] = checks;
        return report;
    }

    fs::path base_dir;
    if (output_dir.is_empty()) {
        base_dir = fs::temp_directory_path() / "circuit_vis_spice_test";
    } else {
        base_dir = fs::path(output_dir.utf8().get_data());
    }
    base_dir = fs::absolute(base_dir).lexically_normal();

    std::time_t now = std::time(nullptr);
    std::string stamp = std::to_string(static_cast<long long>(now));
    fs::path run_dir = base_dir / ("run_" + stamp);
    fs::path include_path = run_dir / "models.inc";
    fs::path spice_path = run_dir / "pipeline_test.spice";

    if (!fs::create_directories(run_dir)) {
        if (!fs::exists(run_dir)) {
            fail(String("Could not create test directory: ") + String(run_dir.string().c_str()));
            report["passed"] = false;
            report["errors"] = errors;
            report["checks"] = checks;
            return report;
        }
    }

    {
        std::ofstream inc(include_path);
        if (!inc.is_open()) {
            fail(String("Could not write include file: ") + String(include_path.string().c_str()));
            report["passed"] = false;
            report["errors"] = errors;
            report["checks"] = checks;
            return report;
        }
        inc << "* include file used by test pipeline\n";
        inc << ".param dummy=1\n";
    }

    {
        std::ofstream spice(spice_path);
        if (!spice.is_open()) {
            fail(String("Could not write spice file: ") + String(spice_path.string().c_str()));
            report["passed"] = false;
            report["errors"] = errors;
            report["checks"] = checks;
            return report;
        }

        spice << "* test deck for run_spice_file\n";
        spice << ".include \"$PDK_ROOT/models.inc\"\n";
        spice << "V1 in 0 PULSE(0 1.8 0\n";
        spice << "+ 1n 1n 5n 10n)\n";
        spice << "R1 in out 1k\n";
        spice << "C1 out 0 1p\n";
        spice << ".control\n";
        spice << "tran 0.1n 20n\n";
        spice << "wrdata out.csv v(in) v(out)\n";
        spice << ".endc\n";
        // Intentionally no .end; pipeline should append it.
    }

    Dictionary result = run_spice_file(
        String(spice_path.string().c_str()),
        String(run_dir.string().c_str())
    );

    bool has_time = result.has("time");
    checks["has_time"] = has_time;
    if (!has_time) {
        fail("Missing time vector in result");
    }

    bool has_vin = result.has("v(in)");
    checks["has_v_in"] = has_vin;
    if (!has_vin) {
        fail("Missing v(in) vector in result");
    }

    bool has_vout = result.has("v(out)");
    checks["has_v_out"] = has_vout;
    if (!has_vout) {
        fail("Missing v(out) vector in result");
    }

    if (has_time && has_vin && has_vout) {
        Array time_data = result["time"];
        Array vin_data = result["v(in)"];
        Array vout_data = result["v(out)"];
        bool non_empty = time_data.size() > 0 && vin_data.size() > 0 && vout_data.size() > 0;
        checks["vectors_non_empty"] = non_empty;
        if (!non_empty) {
            fail("Vectors are empty");
        }

        bool same_length = time_data.size() == vin_data.size() && vin_data.size() == vout_data.size();
        checks["vectors_same_length"] = same_length;
        if (!same_length) {
            fail("Vector lengths do not match");
        }
    }

    String normalized = result.has("normalized_netlist") ? String(result["normalized_netlist"]) : "";
    bool no_control_block = normalized.find(".control") == -1 && normalized.find(".endc") == -1;
    checks["control_removed"] = no_control_block;
    if (!no_control_block) {
        fail("Normalized netlist still contains .control/.endc");
    }

    bool has_tran = normalized.find(".tran 0.1n 20n") != -1;
    checks["tran_appended"] = has_tran;
    if (!has_tran) {
        fail("Expected .tran line not found in normalized netlist");
    }

    bool has_save = normalized.find(".save time v(in) v(out)") != -1;
    checks["save_appended"] = has_save;
    if (!has_save) {
        fail("Expected .save line not found in normalized netlist");
    }

    bool has_end = normalized.find(".end") != -1;
    checks["end_present"] = has_end;
    if (!has_end) {
        fail("Expected .end not found in normalized netlist");
    }

    std::string include_abs = fs::absolute(include_path).lexically_normal().string();
    bool include_rewritten = normalized.find(String(include_abs.c_str())) != -1;
    checks["include_rewritten"] = include_rewritten;
    if (!include_rewritten) {
        fail("Expected absolute include path not found in normalized netlist");
    }

    report["passed"] = errors.is_empty();
    report["errors"] = errors;
    report["checks"] = checks;
    report["test_spice_path"] = String(spice_path.string().c_str());
    report["test_include_path"] = String(include_path.string().c_str());
    report["normalized_netlist"] = normalized;
    return report;
}

String CircuitSimulator::get_current_netlist() const {
    return current_netlist;
}

bool CircuitSimulator::run_simulation() {
    if (!initialized) {
        UtilityFunctions::printerr("ngspice not initialized");
        return false;
    }

    int ret = ng_Command((char*)"bg_run");
    return ret == 0;
}

bool CircuitSimulator::run_transient(double step, double stop, double start) {
    if (!initialized) {
        UtilityFunctions::printerr("ngspice not initialized");
        return false;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "tran %g %g %g", step, stop, start);
    int ret = ng_Command(cmd);

    return ret == 0;
}

bool CircuitSimulator::run_dc(const String &source, double start, double stop, double step) {
    if (!initialized) {
        UtilityFunctions::printerr("ngspice not initialized");
        return false;
    }

    CharString source_utf8 = source.utf8();
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "dc %s %g %g %g", source_utf8.get_data(), start, stop, step);
    int ret = ng_Command(cmd);

    return ret == 0;
}

void CircuitSimulator::pause_simulation() {
    if (!initialized) {
        return;
    }
    ng_Command((char*)"bg_halt");
}

void CircuitSimulator::resume_simulation() {
    if (!initialized) {
        return;
    }
    ng_Command((char*)"bg_resume");
}

void CircuitSimulator::stop_simulation() {
    if (!initialized) {
        return;
    }

    ng_Command((char*)"bg_halt");
    UtilityFunctions::print("Simulation stopped");
}

bool CircuitSimulator::is_running() const {
    if (!initialized || !ng_Running) {
        return false;
    }
    return ng_Running();
}

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

void CircuitSimulator::set_voltage_source(const String &source_name, double voltage) {
    voltage_sources[source_name] = voltage;
    UtilityFunctions::print("Set " + source_name + " to " + String::num(voltage) + "V");
}

double CircuitSimulator::get_voltage_source(const String &source_name) {
    if (voltage_sources.has(source_name)) {
        return (double)voltage_sources[source_name];
    }
    return 0.0;
}

bool CircuitSimulator::set_parameter(const String &name, double value) {
    if (!initialized || !ng_Command) {
        return false;
    }

    CharString name_utf8 = name.utf8();
    std::string command = "alterparam ";
    command += name_utf8.get_data();
    command += "=";
    command += format_double(value);
    int alter_ret = ng_Command((char *)command.c_str());
    if (alter_ret != 0) {
        return false;
    }

    int reset_ret = ng_Command((char *)"reset");
    return reset_ret == 0;
}
