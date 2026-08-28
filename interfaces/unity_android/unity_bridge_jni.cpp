// Copyright 2020-2026, The Board of Trustees of the University of Illinois.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Unity interface entry point for ILLIXR.
 *         Replicates the role of illixr_rt_launch and illixr_hmd_create from
 *         illixr_device.cpp in the Monado driver, adapted for Unity on Android.
 *         Called from Unity C# via [DllImport] rather than from an xrt_device.
 *
 *         Since unity_bridge.so links directly against plugin.main, runtime_factory
 *         is in the same memory space and called directly — no dynamic_lib needed,
 *         mirroring how Monado links against and calls into the ILLIXR library.
 * @author RSIM Group <illixr@cs.illinois.edu>
 */

#ifdef __ANDROID__

#    define DOUBLE_INCLUDE
#    include "illixr/plugin.hpp"
#    include "illixr/runtime.hpp"
#    include "illixr/string_utils.hpp"
#    undef DOUBLE_INCLUDE

#    include <android/log.h>
#    include <dlfcn.h>
#    include <string>
#    include <vector>

#    define UNITY_LOG(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "ILLIXR_Unity", fmt, ##__VA_ARGS__)

// Analogous to ILLIXR_COMP env var read in illixr_prober.c:
// colon-separated list of plugin names to load. runtime_impl::load_so
// reorders these so any plugin whose name contains "network_backend"
// starts first. Extend this list as additional plugins are implemented.
static constexpr const char* ILLIXR_ANDROID_COMP = "tcp_network_backend:android_sensors";
// additional plugins added here as needed, e.g.:
// static constexpr const char* ILLIXR_ANDROID_COMP = "tcp_network_backend:my_plugin";

static ILLIXR::runtime* g_runtime = nullptr;

// runtime_factory is linked in directly from plugin.main —
// no dynamic_lib::create needed, same memory space.
// Analogous to illixr_rt_launch calling
// dynamic_lib::create(ILLIXR_PATH) then get("runtime_factory").
extern "C" ILLIXR::runtime* runtime_factory();

// Defined in unity_component.cpp.
// Analogous to illixr_monado_create_plugin in illixr_component.c.
extern "C" ILLIXR::plugin* illixr_unity_create_plugin(ILLIXR::phonebook*);

extern "C" {

/*!
 * @brief Sets an environment variable for the ILLIXR runtime.
 *        Must be called BEFORE illixr_unity_init() for each variable
 *        that ILLIXR plugins read at startup.
 *
 * @param name   Environment variable name (null-terminated).
 * @param value  Environment variable value (null-terminated).
 * @return       0 on success, -1 on failure.
 */
int illixr_unity_set_env(const char* name, const char* value) {
    if (name == nullptr || value == nullptr) {
        UNITY_LOG("illixr_unity_set_env: null name or value");
        return -1;
    }
    // overwrite = 1: always update, even if already set
    int result = setenv(name, value, 1);
    if (result != 0) {
        UNITY_LOG("illixr_unity_set_env: setenv(%s, %s) failed", name, value);
        return -1;
    }
    UNITY_LOG("illixr_unity_set_env: %s=%s", name, value);
    return 0;
}

/*!
 * @brief Initializes the ILLIXR runtime from Unity.
 *
 * Mirrors the sequence in illixr_rt_launch (illixr_device.cpp in Monado):
 *   1. Call runtime_factory() to create the runtime, phonebook, and switchboard.
 *   2. Load plugin .so files via runtime->load_so. runtime_impl reorders
 *      them so any plugin whose name contains "network_backend" starts first.
 *   3. Register the Unity-side component via runtime->load_plugin_factory.
 *
 * Called from Unity C# MonoBehaviour.Awake() via [DllImport("unity_bridge")].
 *
 * @return 0 on success, non-zero on failure.
 */
int illixr_unity_init() {
    if (g_runtime != nullptr) {
        UNITY_LOG("illixr_unity_init called twice — ignoring");
        return 0;
    }

    setenv("ILLIXR_IS_CLIENT", "1", true);

    // Step 1: create runtime — mirrors illixr_rt_launch calling runtime_factory
    g_runtime = runtime_factory();
    if (g_runtime == nullptr) {
        UNITY_LOG("runtime_factory returned null");
        return -1;
    }

    // Step 2: build lib paths from colon-separated plugin name list,
    // mirroring the lib_paths construction in ILLIXR::run() and the
    // ILLIXR_COMP env var read in illixr_prober.c
    std::vector<std::string> plugin_names = ILLIXR::split(std::string{ILLIXR_ANDROID_COMP}, ':');
    std::vector<std::string> lib_paths;
    lib_paths.reserve(plugin_names.size());
    for (const auto& name : plugin_names)
        lib_paths.push_back("libplugin." + name + ILLIXR_BUILD_SUFFIX_STR + ".so");

    g_runtime->load_so(lib_paths);

    // Step 3: register Unity-side component,
    // mirroring load_plugin_factory(illixr_monado_create_plugin) in illixr_rt_launch
    g_runtime->load_plugin_factory((ILLIXR::plugin_factory) illixr_unity_create_plugin);

    UNITY_LOG("ILLIXR runtime started successfully");
    return 0;
}

/*!
 * @brief Shuts down the ILLIXR runtime.
 *        Called from Unity C# MonoBehaviour.OnApplicationQuit()
 *        via [DllImport("unity_bridge")].
 *        Analogous to illixr_hmd_destroy in illixr_device.cpp.
 */
void illixr_unity_shutdown() {
    if (g_runtime != nullptr) {
        g_runtime->stop();
        delete g_runtime;
        g_runtime = nullptr;
        UNITY_LOG("ILLIXR runtime stopped");
    }
}

// ---------------------------------------------------------------------------
// Depth acquisition pass-throughs
//
// illixr_acquire_depth() and illixr_get_render_event_callback() are defined
// in libplugin.android_sensors.so and loaded into the same process by
// illixr_unity_init(). We resolve them lazily via dlsym so that this bridge
// library has no link-time dependency on the sensor plugin.
// ---------------------------------------------------------------------------

// Function pointer types matching the exports in plugin.cpp
typedef void (*illixr_acquire_depth_fn)();
typedef void* (*illixr_get_render_event_callback_fn)();

static void* resolve_sensor_sym(const char* name) {
    // RTLD_NEXT finds the next occurrence of the symbol after this library,
    // skipping the bridge library itself. This prevents infinite recursion
    // when the bridge library exports a pass-through with the same name —
    // RTLD_DEFAULT would find the pass-through itself, causing a stack overflow.
    void* sym = dlsym(RTLD_NEXT, name);
    if (sym == nullptr)
        UNITY_LOG("resolve_sensor_sym: %s not found via RTLD_NEXT — sensor plugin not loaded?", name);
    return sym;
}

void illixr_acquire_depth() {
    static illixr_acquire_depth_fn fn = nullptr;
    if (fn == nullptr)
        fn = reinterpret_cast<illixr_acquire_depth_fn>(resolve_sensor_sym("illixr_acquire_depth"));
    if (fn != nullptr)
        fn();
}

void* illixr_get_render_event_callback() {
    static illixr_get_render_event_callback_fn fn = nullptr;
    if (fn == nullptr)
        fn = reinterpret_cast<illixr_get_render_event_callback_fn>(resolve_sensor_sym("illixr_get_render_event_callback"));
    return fn != nullptr ? fn() : nullptr;
}

} // extern "C"

#endif // __ANDROID__
