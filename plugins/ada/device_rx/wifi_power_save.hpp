#pragma once

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <spawn.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace ILLIXR::ada::wifi {

struct command_result {
    int         status;
    std::string output;
};

// Run only the selected system executable, without a shell or an interactive
// stdin. Capture small diagnostics and bound the wait during client startup.
inline command_result run_command(const std::vector<std::string>& args,
                                  std::chrono::milliseconds       timeout = std::chrono::seconds{2}) {
    if (args.empty()) {
        return {-1, "missing executable"};
    }
    std::unique_ptr<FILE, decltype(&std::fclose)> output{std::tmpfile(), &std::fclose};
    if (!output) {
        return {-1, std::strerror(errno)};
    }
    // Keep the capture descriptor separate from the child's standard streams,
    // including when the application was launched with a closed stdin/stdout.
    const int capture = fcntl(fileno(output.get()), F_DUPFD_CLOEXEC, 3);
    if (capture < 0) {
        return {-1, std::strerror(errno)};
    }
    posix_spawn_file_actions_t actions;
    int                        error = posix_spawn_file_actions_init(&actions);
    if (error != 0) {
        close(capture);
        return {-1, std::strerror(error)};
    }
    error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (error == 0) {
        error = posix_spawn_file_actions_adddup2(&actions, capture, STDOUT_FILENO);
    }
    if (error == 0) {
        error = posix_spawn_file_actions_adddup2(&actions, capture, STDERR_FILENO);
    }
    if (error == 0) {
        error = posix_spawn_file_actions_addclose(&actions, capture);
    }
    std::vector<char*> argv;
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    pid_t child = -1;
    if (error == 0) {
        error = posix_spawn(&child, args.front().c_str(), &actions, nullptr, argv.data(), ::environ);
    }
    posix_spawn_file_actions_destroy(&actions);
    close(capture);
    if (error != 0) {
        return {-1, std::strerror(error)};
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int        status   = 0;
    while (true) {
        const pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            break;
        }
        if (waited < 0 && errno != EINTR) {
            return {-1, std::strerror(errno)};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) { }
            return {-1, "command timed out"};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    std::rewind(output.get());
    char       buffer[1024];
    const auto size = std::fread(buffer, 1, sizeof(buffer), output.get());
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, std::string{buffer, size}};
}

inline bool valid_interface_name(const std::string& name) {
    return !name.empty() && name.size() < 16 &&
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-") == std::string::npos &&
        name != "." && name != "..";
}

inline bool is_wireless(const std::filesystem::path& interface) {
    return std::filesystem::exists(interface / "phy80211") || std::filesystem::exists(interface / "wireless");
}

inline std::vector<std::string> active_interfaces(const std::filesystem::path& net_directory) {
    std::vector<std::string> interfaces;
    for (const auto& entry : std::filesystem::directory_iterator{net_directory}) {
        if (!is_wireless(entry.path())) {
            continue;
        }
        std::string state;
        std::ifstream{entry.path() / "operstate"} >> state;
        if (state == "up") {
            interfaces.push_back(entry.path().filename().string());
        }
    }
    std::sort(interfaces.begin(), interfaces.end());
    return interfaces;
}

inline std::string system_executable(const std::string& name) {
    for (const auto* directory : {"/usr/sbin/", "/usr/bin/", "/sbin/", "/bin/"}) {
        const auto path = directory + name;
        if (access(path.c_str(), X_OK) == 0) {
            return path;
        }
    }
    return {};
}

inline void require_off_on_interface(const std::string& interface, const std::string& iw) {
    const std::vector<std::string> query{iw, "dev", interface, "get", "power_save"};
    const auto                     result = run_command(query);
    const auto                     begin  = result.output.find_first_not_of(" \t\r\n");
    const auto                     end    = result.output.find_last_not_of(" \t\r\n");
    const auto state = begin == std::string::npos ? std::string{} : result.output.substr(begin, end - begin + 1);
    if (result.status == 0 && state == "Power save: off") {
        spdlog::get("illixr")->info("[Ada Wi-Fi] {} power saving is off", interface);
        return;
    }
    if (result.status == 0 && state == "Power save: on") {
        throw std::runtime_error{"[Ada Wi-Fi] Startup stopped: Wi-Fi power saving is ON on " + interface +
                                 ".\nRun this command in a terminal on this device:\n  sudo " + iw + " dev " + interface +
                                 " set power_save off\nThen restart the Ada client."};
    }
    throw std::runtime_error{"[Ada Wi-Fi] Startup stopped: could not verify Wi-Fi power saving on " + interface +
                             ".\nCheck it on this device with:\n  " + iw + " dev " + interface +
                             " get power_save\nIf it is on, run:\n  sudo " + iw + " dev " + interface +
                             " set power_save off\nThen restart the Ada client.\nQuery result: " + result.output};
}

// Read-only validation before any plugin is constructed or connects to a peer.
// The user changes the setting outside Ada; this code never invokes sudo.
inline void require_power_saving_off(std::string interface) {
    const auto logger = spdlog::get("illixr");
    try {
        if (interface.empty()) {
            const auto interfaces = active_interfaces("/sys/class/net");
            if (interfaces.empty()) {
                logger->info("[Ada Wi-Fi] No active wireless interface; skipping power-save check");
                return;
            }
            if (interfaces.size() != 1) {
                throw std::runtime_error{"[Ada Wi-Fi] Startup stopped: multiple active wireless interfaces. "
                                         "Set ADA_WIFI_INTERFACE in the device configuration to the interface Ada uses."};
            }
            interface = interfaces.front();
        }
        if (!valid_interface_name(interface) || !is_wireless(std::filesystem::path{"/sys/class/net"} / interface)) {
            throw std::runtime_error{"[Ada Wi-Fi] Startup stopped: invalid or unavailable wireless interface '" + interface +
                                     "'. Set ADA_WIFI_INTERFACE to an existing wireless interface."};
        }
        const auto iw = system_executable("iw");
        if (iw.empty()) {
            throw std::runtime_error{"[Ada Wi-Fi] Startup stopped: iw is not installed.\nRun this command on the device:\n"
                                     "  sudo apt-get install iw\nThen restart the Ada client."};
        }
        require_off_on_interface(interface, iw);
    } catch (const std::filesystem::filesystem_error& error) {
        throw std::runtime_error{std::string{"[Ada Wi-Fi] Startup stopped: could not inspect wireless interfaces: "} +
                                 error.what()};
    }
}

} // namespace ILLIXR::ada::wifi
