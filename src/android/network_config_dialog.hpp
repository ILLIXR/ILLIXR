#pragma once

#ifdef __ANDROID__

#    include <android_native_app_glue.h>

#    include <string>
#    include <vector>

namespace ILLIXR {

/**
 * @brief Flags indicating which network backends are present in the plugin list.
 *
 * Constructed by @c make_network_dialog_flags() from the plugin name vector.
 * If neither flag is set, @c show_network_config_dialog() is a no-op.
 */
struct network_dialog_flags {
    bool use_tcp = false; ///< tcp_network_backend is in the plugin list
    bool use_udp = false; ///< udp_network_backend is in the plugin list
};

/**
 * @brief Network configuration collected from the user before any plugins are loaded.
 *
 * Only the fields corresponding to active backends are populated.
 * All active-backend fields are non-empty iff the user confirmed the dialog.
 *
 * @c cancelled is the authoritative "did the user bail?" flag; callers should
 * check it rather than testing individual IP strings for emptiness.
 */
struct network_config {
    bool cancelled = true; ///< true until the user presses Connect

    // TCP fields — populated only when use_tcp was set
    std::string tcp_server_ip;
    std::string tcp_server_port;
    std::string tcp_client_ip;  ///< derived from the device's Wi-Fi interface
    std::string tcp_client_port;

    // UDP fields — populated only when use_udp was set
    std::string udp_server_ip; ///< may equal tcp_server_ip if the user chose "same IP"
    std::string udp_server_port;
    std::string udp_client_ip;  ///< derived from the device's Wi-Fi interface
    std::string udp_client_port;
};

/**
 * @brief Inspects @p plugins and returns flags for whichever network backends are present.
 *
 * @param plugins  The plugin name list that will be passed to @c ILLIXR::run().
 */
network_dialog_flags make_network_dialog_flags(const std::vector<std::string>& plugins);

/**
 * @brief Shows a blocking network configuration dialog using the Android UI thread.
 *
 * Must be called from a thread that is NOT the Android UI thread (i.e., from within
 * the NativeActivity event-loop helper thread). Posts the dialog to the UI thread,
 * then blocks on a latch until the user confirms or cancels.
 *
 * Only the sections corresponding to active backends are shown:
 * - If only @c use_tcp is set, the UDP section is absent.
 * - If only @c use_udp is set, the TCP section is absent.
 * - If both are set, both sections appear; a "same IP as TCP" checkbox (checked
 *   by default) drives whether the UDP server IP field is editable.
 *
 * On confirmation all relevant environment variables are set via @c setenv()
 * before the function returns, so subsequently loaded plugins pick them up:
 * - @c ILLIXR_TCP_SERVER_IP   (when use_tcp)
 * - @c ILLIXR_TCP_SERVER_PORT (when use_tcp)
 * - @c ILLIXR_TCP_CLIENT_IP   (when use_tcp; derived from Wi-Fi)
 * - @c ILLIXR_TCP_CLIENT_PORT (when use_tcp)
 * - @c ILLIXR_UDP_SERVER_IP   (when use_udp)
 * - @c ILLIXR_UDP_SERVER_PORT (when use_udp)
 * - @c ILLIXR_UDP_CLIENT_IP   (when use_udp; derived from Wi-Fi)
 * - @c ILLIXR_UDP_CLIENT_PORT (when use_udp)
 *
 * @param app    Pointer to the android_app struct supplied by android_main.
 * @param flags  Which backend sections to show; obtained from make_network_dialog_flags().
 * @return       Populated network_config with cancelled=false on success;
 *               cancelled=true if the user dismissed the dialog.
 */
network_config show_network_config_dialog(struct android_app* app,
                                          const network_dialog_flags& flags);

} // namespace ILLIXR

#endif // __ANDROID__
