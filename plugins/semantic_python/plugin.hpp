#pragma once

#include "illixr/data_format/semantics.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

#include <pybind11/embed.h>
#include <string>
#include <thread>
#include <unordered_map>

namespace ILLIXR {
class semantic_python : public plugin {
public:
    [[maybe_unused]] explicit semantic_python(const std::string& name, phonebook* pb);
    ~semantic_python() override;

    void start() override;

private:
    void run_python_thread();

    /* Parses a comma-delimited string of key=value and bare-key arguments.
      "config=configs/myconfig.yaml,stride=3,max_frames=100,save_objects"
      produces:
        { "config" -> "configs/myconfig.yaml",
          "stride" -> "3",
          "max_frames" -> "100",
          "save_objects" -> "" }
     */
    void parse_py_args(const std::string& input);

    const std::shared_ptr<switchboard>               switchboard_;
    switchboard::reader<data_format::semantic_data>  semantic_reader_;
    switchboard::reader<data_format::voice_query>    voice_query_reader_;
    switchboard::writer<data_format::query_response> response_writer_;

    pybind11::scoped_interpreter                 guard_;
    std::thread                                  py_thread_;
    std::unordered_map<std::string, std::string> py_args_;
    std::string                                  py_exe_;
};
} // namespace ILLIXR
