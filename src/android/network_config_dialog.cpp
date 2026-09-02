#ifdef __ANDROID__

#    include "network_config_dialog.hpp"

#    include <algorithm>
#    include <android/native_activity.h>
#    include <android_native_app_glue.h>
#    include <cstdlib>
#    include <jni.h>
#    include <spdlog/spdlog.h>
#    include <sstream>
#    include <stdexcept>
#    include <string>
#    include <unistd.h>
#    include <vector>

namespace ILLIXR {

// ---------------------------------------------------------------------------
// Plugin-list inspection
// ---------------------------------------------------------------------------

network_dialog_flags make_network_dialog_flags(const std::vector<std::string>& plugins) {
    network_dialog_flags flags;
    for (const auto& name : plugins) {
        if (name == "tcp_network_backend")
            flags.use_tcp = true;
        if (name == "udp_network_backend")
            flags.use_udp = true;
    }
    return flags;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

    /**
     * @brief Splits @p str on every occurrence of @p delimiter, including empty trailing tokens.
     */
    std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream        ss(str);
        std::string              token;
        while (std::getline(ss, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    /**
     * @brief Calls @c NetworkConfigDialog.showDialog(Activity,boolean,boolean) via JNI.
     *
     * Returns the raw pipe-delimited result string, or an empty string on cancel.
     *
     * @throws std::runtime_error on any JNI lookup failure.
     */
    std::string invoke_java_dialog(struct android_app* app, bool use_tcp, bool use_udp) {
        ANativeActivity* native_activity = app->activity;
        JavaVM*          jvm             = native_activity->vm;
        JNIEnv*          env             = nullptr;

        jint attach_result = jvm->AttachCurrentThread(&env, nullptr);
        if (attach_result != JNI_OK || env == nullptr) {
            throw std::runtime_error("[network_config_dialog] Failed to attach thread to JVM");
        }

        jclass dialog_class = env->FindClass("com/example/ILLIXR/ILLIXRNativeActivity$NetworkConfigDialog");
        if (dialog_class == nullptr) {
            jvm->DetachCurrentThread();
            throw std::runtime_error("[network_config_dialog] Could not find class "
                                     "com/example/ILLIXR/NetworkConfigDialog");
        }

        // Signature: static String showDialog(Activity, boolean useTcp, boolean useUdp)
        jmethodID show_method =
            env->GetStaticMethodID(dialog_class, "showDialog", "(Landroid/app/Activity;ZZ)Ljava/lang/String;");
        if (show_method == nullptr) {
            env->DeleteLocalRef(dialog_class);
            jvm->DetachCurrentThread();
            throw std::runtime_error("[network_config_dialog] Could not find method "
                                     "NetworkConfigDialog.showDialog(Activity,boolean,boolean)");
        }

        jobject activity_object = native_activity->clazz;
        auto    result_jstring  = reinterpret_cast<jstring>(env->CallStaticObjectMethod(
            dialog_class, show_method, activity_object, static_cast<jboolean>(use_tcp), static_cast<jboolean>(use_udp)));

        std::string result;
        if (result_jstring != nullptr) {
            const char* chars = env->GetStringUTFChars(result_jstring, nullptr);
            if (chars != nullptr) {
                result = chars;
                env->ReleaseStringUTFChars(result_jstring, chars);
            }
            env->DeleteLocalRef(result_jstring);
        }

        env->DeleteLocalRef(dialog_class);
        jvm->DetachCurrentThread();
        return result;
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

network_config show_network_config_dialog(struct android_app* app, const network_dialog_flags& flags) {
    network_config cfg; // cancelled = true by default

    if (!flags.use_tcp && !flags.use_udp) {
        // No network backends active — nothing to configure.
        cfg.cancelled = false;
        return cfg;
    }

    std::string raw;
    try {
        raw = invoke_java_dialog(app, flags.use_tcp, flags.use_udp);
    } catch (const std::runtime_error& ex) {
        spdlog::get("illixr")->error("{}", ex.what());
        return cfg; // cancelled = true
    }

    if (raw.empty()) {
        spdlog::get("illixr")->warn("[network_config_dialog] User cancelled network configuration.");
        return cfg; // cancelled = true
    }

    // Wire format (pipe-delimited, fields present only for active backends):
    //
    //   tcp_server_ip | tcp_server_port | tcp_client_port |   <- when use_tcp
    //   udp_server_ip | udp_server_port | udp_client_port |   <- when use_udp
    //   client_ip                                             <- always last
    //
    // Both TCP and UDP use the same physical client IP (the device has one
    // Wi-Fi address); it is transmitted once and stored in both tcp_client_ip
    // and udp_client_ip so that all eight env vars can be set independently.
    //
    // The Java side guarantees fields appear in exactly this order with no
    // gaps, so the total count is: 3*use_tcp + 3*use_udp + 1.
    const int                expected = 3 * (flags.use_tcp ? 1 : 0) + 3 * (flags.use_udp ? 1 : 0) + 1;
    std::vector<std::string> parts    = split(raw, '|');
    if (static_cast<int>(parts.size()) != expected) {
        spdlog::get("illixr")->error("[network_config_dialog] Unexpected result field count {} (expected {}): '{}'",
                                     parts.size(), expected, raw);
        return cfg; // cancelled = true
    }

    int idx = 0;
    if (flags.use_tcp) {
        cfg.tcp_server_ip   = parts[idx++];
        cfg.tcp_server_port = parts[idx++];
        cfg.tcp_client_port = parts[idx++];
    }
    if (flags.use_udp) {
        cfg.udp_server_ip   = parts[idx++];
        cfg.udp_server_port = parts[idx++];
        cfg.udp_client_port = parts[idx++];
    }
    // Both protocol stacks share the device's single Wi-Fi address.
    const std::string& client_ip = parts[idx];
    cfg.tcp_client_ip            = client_ip;
    cfg.udp_client_ip            = client_ip;
    cfg.cancelled                = false;

    // Publish as environment variables so all subsequently loaded plugins
    // find them via the switchboard's getenv() interface.
    if (flags.use_tcp) {
        setenv("ILLIXR_TCP_SERVER_IP", cfg.tcp_server_ip.c_str(), 1);
        setenv("ILLIXR_TCP_SERVER_PORT", cfg.tcp_server_port.c_str(), 1);
        setenv("ILLIXR_TCP_CLIENT_IP", cfg.tcp_client_ip.c_str(), 1);
        setenv("ILLIXR_TCP_CLIENT_PORT", cfg.tcp_client_port.c_str(), 1);
    }
    if (flags.use_udp) {
        setenv("ILLIXR_UDP_SERVER_IP", cfg.udp_server_ip.c_str(), 1);
        setenv("ILLIXR_UDP_SERVER_PORT", cfg.udp_server_port.c_str(), 1);
        setenv("ILLIXR_UDP_CLIENT_IP", cfg.udp_client_ip.c_str(), 1);
        setenv("ILLIXR_UDP_CLIENT_PORT", cfg.udp_client_port.c_str(), 1);
    }

    spdlog::get("illixr")->info("[network_config_dialog] Configuration applied: "
                                "tcp={}:{}/{} (client {}) udp={}:{}/{} (client {})",
                                cfg.tcp_server_ip, cfg.tcp_server_port, cfg.tcp_client_port, cfg.tcp_client_ip,
                                cfg.udp_server_ip, cfg.udp_server_port, cfg.udp_client_port, cfg.udp_client_ip);

    return cfg;
}

} // namespace ILLIXR

#endif // __ANDROID__
