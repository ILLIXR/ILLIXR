#include "illixr/runtime.hpp"

#include "illixr/dynamic_lib.hpp"
#include "illixr/error_util.hpp"
// #include "illixr/extended_window.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/record_logger.hpp"
#include "illixr/stoplight.hpp"
#include "illixr/switchboard.hpp"
#ifndef __ANDROID__
#    include "illixr/vk/vk_extension_request.hpp"
#    include "vulkan_display.hpp"
#endif
// #include "sqlite_record_logger.hpp"
// #include "stdout_record_logger.hpp"
#include "no_op_record_logger.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#ifndef __ANDROID__
#    include <set>
#endif
#include <spdlog/spdlog.h>
#ifdef __ANDROID__
#    include <spdlog/sinks/android_sink.h>
#else
#    include <spdlog/sinks/basic_file_sink.h>
#    include <spdlog/sinks/stdout_color_sinks.h>
#endif
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#    include <cstdlib>
#endif

using namespace ILLIXR;
#ifndef __ANDROID__
typedef bool (*n_monado_t)();
#endif

void spdlogger(const std::string& name, const char* log_level) {
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
    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/" + name + ".log"));
#endif
    auto logger = std::make_shared<spdlog::logger>(name, begin(sinks), end(sinks));
    logger->set_level(spdlog::level::from_str(log_level));
    spdlog::register_logger(logger);
}

class MY_EXPORT_API runtime_impl : public runtime {
public:
    explicit runtime_impl() {
        spdlogger("illixr", std::getenv("ILLIXR_LOG_LEVEL")); // can't use switchboard interface here
        phonebook_.register_impl<relative_clock>(std::make_shared<relative_clock>());
        phonebook_.register_impl<record_logger>(std::make_shared<no_op_record_logger>());
        phonebook_.register_impl<switchboard>(std::make_shared<switchboard>(&phonebook_));
#if !defined(ENABLE_MONADO) && !defined(__ANDROID__)
        // phonebook_.register_impl<xlib_gl_extended_window>(
        //     std::make_shared<xlib_gl_extended_window>(display_params::width_pixels, display_params::height_pixels, nullptr));
#endif
        switchboard_ = phonebook_.lookup_impl<switchboard>();
#ifndef __ANDROID__
        enable_monado_ = false;
#endif
        phonebook_.register_impl<stoplight>(std::make_shared<stoplight>());
    }

    void load_so(const std::vector<std::string>& so_paths) override {
        RAC_ERRNO_MSG("runtime_impl before creating any dynamic library");

        std::transform(so_paths.cbegin(), so_paths.cend(), std::back_inserter(libraries_), [](const auto& so_path) {
            RAC_ERRNO_MSG("runtime_impl before creating the dynamic library");
            return dynamic_lib::create(so_path);
        });
#ifndef __ANDROID__
        for (auto& i : libraries_) {
            enable_monado_ = enable_monado_ || i.get<n_monado_t>("needs_monado")();
        }
#endif
        RAC_ERRNO_MSG("runtime_impl after creating the dynamic libraries");

        std::vector<plugin_factory> plugin_factories;
        std::transform(libraries_.cbegin(), libraries_.cend(), std::back_inserter(plugin_factories), [](const auto& lib) {
            return lib.template get<plugin* (*) (phonebook*)>("this_plugin_factory");
        });

#ifndef __ANDROID__
        if (!enable_monado_) {
            // get env var ILLIXR_DISPLAY_MODE
            std::string display_mode =
                switchboard_->get_env_char("ILLIXR_DISPLAY_MODE") ? switchboard_->get_env_char("ILLIXR_DISPLAY_MODE") : "glfw";
            if (display_mode != "none")
                phonebook_.register_impl<vulkan::display_provider>(std::make_shared<display_vk>(&phonebook_));
        }
#endif
        RAC_ERRNO_MSG("runtime_impl after generating plugin factories");
        phonebook_.lookup_impl<relative_clock>()->start();

        int plugin_offset = 0;
#ifdef __ANDROID__ // on Android we have to have the network plugins up and running right away
        plugins_.push_back(std::unique_ptr<plugin>{plugin_factories[0](&phonebook_)});
        plugins_[0]->start();
        plugins_.push_back(std::unique_ptr<plugin>{plugin_factories[1](&phonebook_)});
        plugins_[1]->start();
        plugin_offset = 2;
#endif

        std::transform(plugin_factories.cbegin() + plugin_offset, plugin_factories.cend(), std::back_inserter(plugins_),
                       [this](const auto& plugin_factory) {
                           RAC_ERRNO_MSG("runtime_impl before building the plugin");
                           try {
#ifdef __ANDROID__
                               return std::unique_ptr<plugin>{plugin_factory(&phonebook_)};
#else
                               return std::unique_ptr<plugin>{plugin_factory(&this->phonebook_)};
#endif
                           } catch (std::exception& ex) {
                               spdlog::get("illixr")->error(ex.what());
                               throw;
                           }
                       });

#ifndef __ANDROID__
        if (!enable_monado_) {
            const std::string display_mode =
                switchboard_->get_env_char("ILLIXR_DISPLAY_MODE") ? switchboard_->get_env_char("ILLIXR_DISPLAY_MODE") : "glfw";
            if (display_mode != "none") {
                std::set<const char*> instance_extensions;
                std::set<const char*> device_extensions;

                std::for_each(plugins_.cbegin(), plugins_.cend(), [&](const auto& plugin) {
                    auto requester = std::dynamic_pointer_cast<ILLIXR::vulkan::vk_extension_request>(plugin);
                    if (requester != nullptr) {
                        auto requested_instance_extensions = requester->get_required_instance_extensions();
                        instance_extensions.insert(requested_instance_extensions.begin(), requested_instance_extensions.end());

                        auto requested_device_extensions = requester->get_required_devices_extensions();
                        device_extensions.insert(requested_device_extensions.begin(), requested_device_extensions.end());
                    }
                });

                auto display = std::static_pointer_cast<display_vk>(phonebook_.lookup_impl<vulkan::display_provider>());
                display->start(instance_extensions, device_extensions);
            }
        }
#endif
        std::for_each(plugins_.cbegin() + plugin_offset, plugins_.cend(), [](const auto& plugin) {
            // Well-behaved plugins_ (any derived from threadloop) start there threads here, and then wait on the Stoplight.
            plugin->start();
        });

        // This actually kicks off the plugins
        phonebook_.lookup_impl<stoplight>()->signal_ready();
    }

    void load_so(const std::string_view& so) override {
        auto lib                 = dynamic_lib::create(so);
        auto this_plugin_factory = lib.get<plugin* (*) (phonebook*)>("this_plugin_factory");
        load_plugin_factory(this_plugin_factory);
        libraries_.push_back(std::move(lib));
    }

    void load_plugin_factory(plugin_factory plugin_main) override {
        plugins_.emplace_back(plugin_main(&phonebook_));
        plugins_.back()->start();
    }

    void wait() override {
        const std::shared_ptr<stoplight> stoplight = phonebook_.lookup_impl<ILLIXR::stoplight>();
        // Plugins request an application-wide shutdown by signaling should_stop.
        // Coordinate the actual stop from this caller thread so a requesting
        // plugin never attempts to join its own worker thread.
        stoplight->wait_for_should_stop();
        _stop();
        // Do not return until every plugin thread has been joined.
        stoplight->wait_for_shutdown_complete();
    }

    void _stop() override {
        std::call_once(stop_once_, [this]() {
            phonebook_.lookup_impl<stoplight>()->signal_should_stop();
            // After this point, threads may exit their main loops
            // They still have destructors and still have to be joined.

            phonebook_.lookup_impl<switchboard>()->stop();
            // After this point, Switchboard's internal thread-workers which power synchronous callbacks are stopped and joined.

            for (const std::shared_ptr<plugin>& plugin : plugins_) {
                plugin->stop();
                // Each plugin gets joined in its stop
            }

            // Tell runtime::wait() that it can return
            phonebook_.lookup_impl<stoplight>()->signal_shutdown_complete();
        });
    }

    ~runtime_impl() override {
        if (!phonebook_.lookup_impl<stoplight>()->check_shutdown_complete()) {
            stop();
        }
        // This will be re-enabled in #225
        // assert(errno == 0 && "errno was set during run. Maybe spurious?");
        /*
          Note that this assertion can have false positives AND false negatives!
          - False positive because the contract of some functions specifies that errno is only meaningful if the return code was
          an error [1].
            - We will try to mitigate this by clearing errno on success in ILLIXR.
          - False negative if some intervening call clears errno.
            - We will try to mitigate this by checking for errors immediately after a call.

          Despite these mitigations, the best way to catch errors is to check errno immediately after a calling function.

          [1] https://cboard.cprogramming.com/linux-programming/119957-xlib-perversity.html
         */
    }

private:
    // I have to keep the dynamic libraries in scope until the program is dead
    std::vector<dynamic_lib>             libraries_;
    phonebook                            phonebook_;
    std::vector<std::shared_ptr<plugin>> plugins_;
    std::once_flag                       stop_once_;
};

extern "C" [[maybe_unused]] MY_EXPORT_API runtime* runtime_factory() {
    RAC_ERRNO_MSG("runtime_impl before creating the runtime");
    return new runtime_impl{};
}
