#include "common/dynamic_lib.hpp"
#include "common/error_util.hpp"
#include "common/extended_window.hpp"
#include "common/global_module_defs.hpp"
#include "common/plugin.hpp"
#include "common/relative_clock.hpp"
#include "common/runtime.hpp"
#include "common/stoplight.hpp"
#include "common/switchboard.hpp"
#include "noop_record_logger.hpp"
#include "sqlite_record_logger.hpp"
#include "stdout_record_logger.hpp"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <thread>

using namespace ILLIXR;

class runtime_impl : public runtime {
public:
    runtime_impl(
#ifndef ILLIXR_MONADO_MAINLINE
        GLXContext appGLCtx
#endif /// ILLIXR_MONADO_MAINLINE
    ) {
        pb.register_impl<record_logger>(std::make_shared<sqlite_record_logger>());
        pb.register_impl<gen_guid>(std::make_shared<gen_guid>());
        pb.register_impl<switchboard>(std::make_shared<switchboard>(&pb));
#ifndef ILLIXR_MONADO_MAINLINE
        pb.register_impl<xlib_gl_extended_window>(
            std::make_shared<xlib_gl_extended_window>(display_params::width_pixels, display_params::height_pixels, appGLCtx));
#endif /// ILLIXR_MONADO_MAINLINE
        pb.register_impl<Stoplight>(std::make_shared<Stoplight>());
        pb.register_impl<RelativeClock>(std::make_shared<RelativeClock>());
    }

    virtual void load_so(const std::vector<std::string>& so_paths) override {
        RAC_ERRNO_MSG("runtime_impl before creating any dynamic library");

        std::transform(so_paths.cbegin(), so_paths.cend(), std::back_inserter(libs), [](const auto& so_path) {
            RAC_ERRNO_MSG("runtime_impl before creating the dynamic library");
            return dynamic_lib::create(so_path);
        });

        RAC_ERRNO_MSG("runtime_impl after creating the dynamic libraries");

        std::vector<plugin_factory> plugin_factories;
        std::transform(libs.cbegin(), libs.cend(), std::back_inserter(plugin_factories), [](const auto& lib) {
            return lib.template get<plugin* (*) (phonebook*)>("this_plugin_factory");
        });

        RAC_ERRNO_MSG("runtime_impl after generating plugin factories");

        std::transform(plugin_factories.cbegin(), plugin_factories.cend(), std::back_inserter(plugins),
                       [this](const auto& plugin_factory) {
                           RAC_ERRNO_MSG("runtime_impl before building the plugin");
                           return std::unique_ptr<plugin>{plugin_factory(&pb)};
                       });

        std::for_each(plugins.cbegin(), plugins.cend(), [](const auto& plugin) {
            // Well-behaved plugins (any derived from threadloop) start there threads here, and then wait on the Stoplight.
            plugin->start();
        });

        // This actually kicks off the plugins
        pb.lookup_impl<RelativeClock>()->start();
        pb.lookup_impl<Stoplight>()->signal_ready();
    }

    virtual void load_so(const std::string_view so) override {
        auto           lib                 = dynamic_lib::create(so);
        plugin_factory this_plugin_factory = lib.get<plugin* (*) (phonebook*)>("this_plugin_factory");
        load_plugin_factory(this_plugin_factory);
        libs.push_back(std::move(lib));
    }

    virtual void load_plugin_factory(plugin_factory plugin_main) override {
        plugins.emplace_back(plugin_main(&pb));
        plugins.back()->start();
    }

    virtual void wait() override {
        // We don't want wait() returning before all the plugin threads have been joined.
        // That would cause a nasty race-condition if the client tried to delete the runtime right after wait() returned.
        pb.lookup_impl<Stoplight>()->wait_for_shutdown_complete();
    }

    virtual void stop() override {
        pb.lookup_impl<Stoplight>()->signal_should_stop();
        // After this point, threads may exit their main loops
        // They still have destructors and still have to be joined.

        pb.lookup_impl<switchboard>()->stop();
        // After this point, Switchboard's internal thread-workers which power synchronous callbacks are stopped and joined.

        for (const std::unique_ptr<plugin>& plugin : plugins) {
            plugin->stop();
            // Each plugin gets joined in its stop
        }

        // Tell runtime::wait() that it can return
        pb.lookup_impl<Stoplight>()->signal_shutdown_complete();
    }

    virtual ~runtime_impl() override {
        if (!pb.lookup_impl<Stoplight>()->check_shutdown_complete()) {
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
    // I have to keep the dynamic libs in scope until the program is dead
    std::vector<dynamic_lib>             libs;
    phonebook                            pb;
    std::vector<std::unique_ptr<plugin>> plugins;
};

#ifdef ILLIXR_MONADO_MAINLINE
extern "C" runtime* runtime_factory() {
    RAC_ERRNO_MSG("runtime_impl before creating the runtime");
    return new runtime_impl{};
}
#else
extern "C" runtime* runtime_factory(GLXContext appGLCtx) {
    RAC_ERRNO_MSG("runtime_impl before creating the runtime");
    return new runtime_impl{appGLCtx};
}
#endif /// ILLIXR_MONADO_MAINLINE
