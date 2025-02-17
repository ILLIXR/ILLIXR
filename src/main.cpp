#include "illixr.hpp"

#include <csignal>
#include <iostream>

/**
 * @brief A signal handler for SIGINT.
 *
 * Stops the execution of the application via the ILLIXR runtime.
 */
static void sigint_handler([[maybe_unused]] int sig) {
    assert(sig == SIGINT && "sigint_handler is for SIGINT");
    if (runtime_) {
        runtime_->stop();
    }
}

int main(int argc, const char* argv[]) {
    cxxopts::Options options("ILLIXR", "Main program");
    options.show_positional_help();
    // std::string illixr_data, illixr_demo_data, realsense_cam;
    // illixr_data = illixr_demo_data = realsense_cam = "";
    // bool offload_enable, alignment_enable, enable_verbose_errors, enable_pre_sleep;
    // offload_enable = alignment_enable = enable_verbose_errors = enable_pre_sleep = false;
    // long run_dur = 0;
    options.add_options()("d,duration", "The duration to run for", cxxopts::value<long>())(
        "data", "The data", cxxopts::value<std::string>())("demo_data", "The demo data", cxxopts::value<std::string>())(
        "enable_offload", "")("enable_alignment", "")("enable_verbose_errors", "")("enable_pre_sleep", "")(
        "h,help", "Produce help message")("realsense_cam", "", cxxopts::value<std::string>()->default_value("auto"))(
        "p,plugins", "The plugins to use", cxxopts::value<std::vector<std::string>>())(
        "vis", "The visualizer to use", cxxopts::value<std::vector<std::string>>())("y,yaml", "Yaml config file",
                                                                                    cxxopts::value<std::string>())(
        "r,run",
        "The plugins to run, supersedes plugins entry. This is only necessary if a plugin builds more than one library (e.g. "
        "offload_vio builds 4 libraries) as each must be loaded individually.");
    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return EXIT_SUCCESS;
    }

    /// Shutting down method 1: Ctrl+C
    std::signal(SIGINT, sigint_handler);

    return ILLIXR::run(result);
}
