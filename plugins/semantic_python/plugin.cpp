#include "plugin.hpp"

#include "switchboard_bindings.hpp"

#include <pybind11/pybind11.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] semantic_python::semantic_python(const std::string& name, ILLIXR::phonebook* pb)
    : plugin{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , semantic_reader_{switchboard_->get_reader<semantic_data>("semantic_data")}
    , voice_query_reader_{switchboard_->get_reader<voice_query>("semantic_query")}
    , response_writer_{switchboard_->get_writer<query_response>("semantic_response")}
    , guard_{} {
    std::string arg_string = switchboard_->get_env("SEMANTIC_PYTHON_ARGS", "");
    parse_py_args(arg_string);
    py_exe_ = switchboard_->get_env("SEMANTIC_PYTHON_EXE", "");
    if (py_exe_.empty())
        throw std::runtime_error("No SEMANTIC_PYTHON_EXE given.");
}

semantic_python::~semantic_python() {
    if (py_thread_.joinable())
        py_thread_.join();
}

void semantic_python::start() {
    py_thread_ = std::thread(&semantic_python::run_python_thread, this);
}

void semantic_python::run_python_thread() {
    pybind11::gil_scoped_acquire acquire;
    try {
        pybind11::list argv;
        argv.append(py_exe_);
        for (const auto& [key, val] : py_args_) {
            if (key.size() == 1) {
                argv.append("-" + key);
            } else {
                argv.append("--" + key);
            }
            if (!val.empty())
                argv.append(val);
        }
        pybind11::module_::import("sys").attr("argv") = argv;

        // register the module
        auto m = pybind11::module_::create_extension_module("illixr_bridge", nullptr, new pybind11::module_::module_def{});
        register_bindings(m);

        // inject the proxy objects
        py_semantic_data_reader  semantic_proxy{&semantic_reader_};
        py_voice_query_reader    voice_proxy{&voice_query_reader_};
        py_query_response_writer response_proxy{&response_writer_};

        pybind11::dict globals            = pybind11::globals();
        globals["illixr_semantic_reader"] = pybind11::cast(semantic_proxy);
        globals["illixr_voice_reader"]    = pybind11::cast(voice_proxy);
        globals["illixr_response_writer"] = pybind11::cast(response_proxy);

        spdlog::get("illixr")->info("[python_bridge] Starting script: {}", py_exe_);
        pybind11::eval_file(py_exe_, globals);
    } catch (const pybind11::error_already_set& e) {
        spdlog::get("illixr")->error("[python_bridge] Python exception: {}", e.what());
    } catch (const std::exception& e) {
        spdlog::get("illixr")->error("[python_bridge] Exception: {}", e.what());
    }
}

void semantic_python::parse_py_args(const std::string& input) {
    std::string::size_type start = 0;
    while (start < input.size()) {
        auto comma = input.find(',', start);
        if (comma == std::string::npos)
            comma = input.size();
        std::string token = input.substr(start, comma - start);

        if (!token.empty()) {
            auto eq = token.find('=');
            if (eq == std::string::npos) {
                py_args_[token] = "";
            } else if (eq == 0) {
                spdlog::get("illixr")->warn("Malformed SEMANTIC_PYTHON_ARGS token '{}'", token);
            } else {
                py_args_[token.substr(0, eq)] = token.substr(eq + 1);
            }
        }
        start = comma + 1;
    }
}

PLUGIN_MAIN(semantic_python)
