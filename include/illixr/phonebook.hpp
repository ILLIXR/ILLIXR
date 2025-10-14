#pragma once

#include "export.hpp"

#include <cassert>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#include <cstdlib>
#endif

#ifndef NDEBUG
    #include <iostream>
    #include <spdlog/spdlog.h>
// #include "spdlog/sinks/stdout_color_sinks.h"
#endif

namespace ILLIXR {

#if defined(_WIN32) || defined(_WIN64)
static void setenv(const std::string& var, const std::string& val, int) {
    std::string env_stmt = var + "=" + val;
    putenv(env_stmt.c_str());
}
#endif

/**
 * @brief A [service locator][1] for ILLIXR.
 *
 * This will be explained through an example: Suppose one dynamically-loaded plugin, `A_plugin`,
 * needs a service, `B_service`, provided by another, `B_plugin`. `A_plugin` cannot statically
 * construct a `B_service`, because the implementation `B_plugin` is dynamically
 * loaded. However, `B_plugin` can register an implementation of `B_service` when it is loaded,
 * and `A_plugin` can lookup that implementation without knowing it.
 *
 * `B_service.hpp` in `common`:
 * \code{.cpp}
 * class B_service {
 * public:
 *     virtual void frobnicate(foo data) = 0;
 * };
 * \endcode
 *
 * `B_plugin.hpp`:
 * \code{.cpp}
 * class B_impl : public B_service {
 * public:
 *     virtual void frobnicate(foo data) {
 *         // ...
 *     }
 * };
 * void blah_blah(phonebook* pb) {
 *     // Expose `this` as the "official" implementation of `B_service` for this run.
 *     pb->register_impl<B_service>(std::make_shared<B_impl>());
 * }
 * \endcode
 *
 * `A_plugin.cpp`:
 * \code{.cpp}
 * #include "B_service.hpp"
 * void blah_blah(phonebook* pb) {
 *     B_service* b = pb->lookup_impl<B_service>();
 *     b->frobnicate(data);
 * }
 * \endcode
 *
 * If the implementation of `B_service` is not known to `A_plugin` (the usual case), `B_service
 * should be an [abstract class][2]. In either case `B_service` should be in `common`, so both
 * plugins can refer to it.
 *
 * One could even selectively return a different implementation of `B_service` depending on the
 * caller (through the parameters), but we have not encountered the need for that yet.
 *
 * [1]: https://en.wikipedia.org/wiki/Service_locator_pattern
 * [2]: https://en.wikibooks.org/wiki/C%2B%2B_Programming/Classes/Abstract_Classes
 */
class MY_EXPORT_API phonebook {
    /*
      Proof of thread-safety:
      - Since all instance members are private, acquiring a lock in each method implies the class is datarace-free.
      - Since there is only one lock and this does not call any code containing locks, this is deadlock-free.
      - Both of these methods are only used during initialization, so the locks are not contended in steady-state.

      However, to write a correct program, one must also check the thread-safety of the elements
      inserted into this class by the caller.
    */

public:
    /**
     * @brief A 'service' that can be registered in the phonebook.
     *
     * These must be 'destructible', have a virtual destructor that phonebook can call in its
     * destructor.
     */
    class service {
    public:
        // auto spdlogger(const char* log_level) {
        //     if (!log_level) {
        // #ifdef NDEBUG
        //         log_level = "warn";
        // #else
        //         log_level = "debug";
        // #endif
        //     }
        //     std::vector<spdlog::sink_ptr> sinks;
        //     // auto                          file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/" +
        //     type_index.name() + ".log"); auto                          console_sink =
        //     std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        //     // sinks.push_back(file_sink);
        //     sinks.push_back(console_sink);
        //     plugin_logger = std::make_shared<spdlog::logger>(type_index.name(), begin(sinks), end(sinks));
        //     plugin_logger->set_level(spdlog::level::from_str(log_level));
        //     spdlog::register_logger(plugin_logger);
        //     return plugin_logger;
        // }

        // void spd_add_file_sink(const std::string& file_name, const std::string& extension, const std::string& log_level) {
        //     if (!plugin_logger) {
        //         throw std::runtime_error("Logger not found");
        //     }

        // auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/" + file_name + "." + extension, true);
        // file_sink->set_level(spdlog::level::from_str(log_level));
        // plugin_logger->sinks().push_back(file_sink);
        // size_t sink_count = plugin_logger->sinks().size();
        // plugin_logger->sinks()[sink_count-1]->set_pattern("%v");
        // }

        virtual ~service() = default;

        // std::shared_ptr<spdlog::logger>      plugin_logger;
    };

    /**
     * @brief Registers an implementation of @p baseclass for future calls to lookup.
     *
     * Safe to be called from any thread.
     *
     * The implementation will be owned by phonebook (phonebook calls `delete`).
     */
    template<typename Specific_service>
    void register_impl(std::shared_ptr<Specific_service> impl) {
        const std::unique_lock<std::shared_mutex> lock{mutex_};

        const std::type_index type_index = std::type_index(typeid(Specific_service));
#ifndef NDEBUG
        spdlog::get("illixr")->debug("[phonebook] Register {}", type_index.name());
#endif
        assert(registry_.count(type_index) == 0);
        registry_.try_emplace(type_index, impl);
    }

    /**
     * @brief Look up an implementation of @p Specific_service, which should be registered first.
     *
     * Safe to be called from any thread.
     *
     * Do not call `delete` on the returned object; it is still managed by phonebook.
     *
     * @throws if an implementation is not already registered.
     */
    template<typename Specific_service>
    std::shared_ptr<Specific_service> lookup_impl() const {
        const std::shared_lock<std::shared_mutex> lock{mutex_};

        const std::type_index type_index = std::type_index(typeid(Specific_service));

#ifndef NDEBUG
        // if this fails, and there are no duplicate base classes, ensure the hash_code's are unique.
        if (registry_.count(type_index) != 1) {
            throw std::runtime_error{"Attempted to lookup an unregistered implementation " + std::string{type_index.name()}};
        }
#endif

        std::shared_ptr<service> this_service = registry_.at(type_index);
        if (!static_cast<bool>(this_service))
            throw std::runtime_error{"Could not find " + std::string{type_index.name()}};

        std::shared_ptr<Specific_service> this_specific_service = std::dynamic_pointer_cast<Specific_service>(this_service);
        if (!static_cast<bool>(this_service))
            throw std::runtime_error{"Could not find specific " + std::string{type_index.name()}};

        return this_specific_service;
    }

    template<typename specific_service>
    [[maybe_unused]] bool has_impl() const {
        const std::shared_lock<std::shared_mutex> lock{mutex_};

        const std::type_index type_index = std::type_index(typeid(specific_service));

        return registry_.count(type_index) == 1;
    }

private:
    std::unordered_map<std::type_index, const std::shared_ptr<service>> registry_;
    mutable std::shared_mutex                                           mutex_;
};
} // namespace ILLIXR
