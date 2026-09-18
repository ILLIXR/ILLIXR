/**
 * @brief Shared unit test runner for Linux/Windows and Android.
 *
 * Only two things differ by platform: how the set of candidate unit test libraries is discovered
 * (a directory scan next to this binary on Linux/Windows, a list already known at compile time on
 * Android -- see unit_test_manifest.hpp), and the process entry point itself (main() vs
 * android_main()). Everything else -- loading each candidate, validating it via
 * this_test_component(), shuffling the discovered set, and running every registered GoogleTest
 * case in one process -- is identical, matching how main.cpp/plugin.cpp/runtime_impl.cpp already
 * split their platform differences along __ANDROID__.
 */

#include "illixr/dynamic_lib.hpp"

#include <algorithm>
#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>
#include <random>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#ifdef __ANDROID__
#    include "unit_test_manifest.hpp"

#    include <android_native_app_glue.h>
#    include <spdlog/sinks/android_sink.h>
#else
#    include <filesystem>

#    include <spdlog/sinks/basic_file_sink.h>
#    include <spdlog/sinks/stdout_color_sinks.h>
#    if defined(_WIN32) || defined(_WIN64)
#        include <Windows.h>
#    endif
#endif

namespace ILLIXR {

namespace {

/**
 * @brief Registers the "illixr" spdlog logger, mirroring runtime_impl.cpp's spdlogger() helper.
 *
 * Duplicated here rather than shared, since the original is a file-local free function in
 * runtime_impl.cpp with no header declaring it. dynamic_lib's destructor logs through
 * spdlog::get("illixr"), so this must run before any dynamic_lib is constructed or destroyed.
 */
void setup_logger() {
    const char* log_level = std::getenv("ILLIXR_LOG_LEVEL");
    if (!log_level) {
#ifdef NDEBUG
        log_level = "warn";
#else
        log_level = "debug";
#endif
    }

    std::vector<spdlog::sink_ptr> sinks;
#ifdef __ANDROID__
    sinks.push_back(std::make_shared<spdlog::sinks::android_sink_mt>());
#else
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/unit_test_runner.log"));
#endif
    auto logger = std::make_shared<spdlog::logger>("illixr", begin(sinks), end(sinks));
    logger->set_level(spdlog::level::from_str(log_level));
    spdlog::register_logger(logger);
}

#ifndef __ANDROID__
/**
 * @brief The directory containing the currently-running executable.
 *
 * Deliberately not std::filesystem::current_path(): the runner needs to scan the unit_tests/
 * output directory it was built into, regardless of the working directory it happens to be
 * invoked from.
 */
std::filesystem::path executable_directory() {
#    if defined(_WIN32) || defined(_WIN64)
    std::string path(MAX_PATH, '\0');
    DWORD       length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
#    else
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#    endif
}

/**
 * @brief True if path names a shared library this platform's dynamic loader can load.
 */
bool is_shared_library(const std::filesystem::path& path) {
#    if defined(_WIN32) || defined(_WIN64)
    return path.extension() == ".dll";
#    else
    return path.extension() == ".so";
#    endif
}
#endif // __ANDROID__

/**
 * @brief Builds the list of candidate unit test libraries to attempt to load.
 *
 * On Linux/Windows this scans the directory the runner itself lives in (the same unit_tests/
 * output directory every add_illixr_unit_test() target is placed in), which will also contain
 * illixr_gtest/illixr_gmock themselves -- those get filtered out naturally in
 * load_unit_test_libraries() below, since they don't export this_test_component(). On Android
 * every candidate was already a link-time dependency of unit-tests-activity, so the list is
 * simply whatever CMake compiled into unit_test_manifest.hpp from UNIT_TEST_LIST.
 */
std::vector<std::string> discover_candidates() {
    std::vector<std::string> candidates;

#ifdef __ANDROID__
    candidates.assign(k_unit_test_libraries.begin(), k_unit_test_libraries.end());
#else
    for (const auto& entry : std::filesystem::directory_iterator(executable_directory())) {
        if (entry.is_regular_file() && is_shared_library(entry.path())) {
            candidates.push_back(entry.path().string());
        }
    }
#endif

    return candidates;
}

/**
 * @brief Loads and validates every candidate, discarding anything that isn't really one of ours.
 *
 * A candidate is kept only if this_test_component() resolves successfully -- this is how a
 * genuine unit test library is told apart from illixr_gtest/illixr_gmock or any other unrelated
 * file that might be sitting alongside them (see UNIT_TEST_MAIN in include/illixr/unit_test.hpp).
 * Loaded libraries are kept alive for the caller's lifetime: their TEST()/TEST_F() cases
 * registered into the shared GoogleTest registry as a side effect of loading, and unloading
 * before RUN_ALL_TESTS() would tear those registrations back down.
 */
std::vector<dynamic_lib> load_unit_test_libraries(std::vector<std::string> candidates) {
    std::random_device rd;
    std::mt19937       gen(rd());
    std::shuffle(candidates.begin(), candidates.end(), gen);

    std::vector<dynamic_lib> loaded;
    loaded.reserve(candidates.size());

    for (const auto& candidate : candidates) {
        try {
            dynamic_lib lib               = dynamic_lib::create(candidate);
            auto        get_component_name = lib.get<const char* (*) ()>("this_test_component");
            spdlog::get("illixr")->info("[unit_test_runner] loaded {} ({})", get_component_name(), candidate);
            loaded.push_back(std::move(lib));
        } catch (const std::exception& e) {
            spdlog::get("illixr")->debug("[unit_test_runner] skipping {}: {}", candidate, e.what());
        }
    }

    return loaded;
}

int run(int argc, char** argv) {
    setup_logger();

    auto candidates = discover_candidates();
    spdlog::get("illixr")->info("[unit_test_runner] found {} candidate librar{}", candidates.size(),
                                candidates.size() == 1 ? "y" : "ies");

    // Deliberately leaked, never dlclose()'d: GoogleTest's own registry keeps live references --
    // TestInfo/TestCase objects whose vtables live inside these libraries -- until its own static
    // teardown at actual process exit, which happens after this function returns. Unloading any
    // earlier unmaps that code while the registry still points into it, segfaulting during the
    // C++ runtime's global destruction phase. This process exits immediately after RUN_ALL_TESTS()
    // regardless, so the OS reclaims everything either way; this is standard practice for
    // dlopen-based systems with global registries (the same reason plugin.hpp-style systems
    // generally never dlclose() things that registered global state).
    auto& loaded = *(new std::vector<dynamic_lib>(load_unit_test_libraries(std::move(candidates))));
    spdlog::get("illixr")->info("[unit_test_runner] {} unit test librar{} validated and loaded", loaded.size(),
                                loaded.size() == 1 ? "y" : "ies");

    ::testing::InitGoogleTest(&argc, argv);
    GTEST_FLAG_SET(shuffle, true);

    return RUN_ALL_TESTS();}

} // namespace

} // namespace ILLIXR

#ifdef __ANDROID__

void android_main(struct android_app* state) {
    char  arg0[] = "unit_test_runner";
    char* argv[] = {arg0};
    int   result = ILLIXR::run(1, argv);

    spdlog::get("illixr")->info("[unit_test_runner] finished with exit code {}", result);

    // TODO: surface a pass/fail summary on screen instead of exiting immediately.
    ANativeActivity_finish(state->activity);
}

#else

int main(int argc, char** argv) {
    return ILLIXR::run(argc, argv);
}

#endif
