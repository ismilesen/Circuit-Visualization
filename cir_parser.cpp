/*
 * ngSpice_Circ + in-memory normalization (xschem-ish .spice)
 *
 * Implements:
 *  1) Read uploaded file lines
 *  2) Build logical lines (handle + continuation)
 *  3) Transform lines in memory:
 *     - rewrite .lib/.include paths (expand $PDK_ROOT)
 *     - rewrite input_file="..." to absolute (same resolver)
 *     - remove .control ... .endc and extract tran + wrdata signal names
 *  4) Append: .tran, .save, .end
 *  5) Convert to char** + NULL
 *  6) ngSpice_Circ(lines)
 *  7) ngSpice_Command("run")
 *  8) Query vectors and write CSV
 *
 * Notes:
 *  - This is designed for transient (tran) + voltage vectors v(node).
 */

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

extern
