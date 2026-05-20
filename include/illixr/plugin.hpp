#pragma once

#include "phonebook.hpp"
#include "record_logger.hpp"

#include <memory>
#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#ifdef __ANDROID__
#  include <spdlog/sinks/android_sink.h>
#else
#  include <spdlog/sinks/stdout_color_sinks.h>
#endif
#include <spdlog/spdlog.h>
#include <string>
#include <typeinfo>
#include <utility>

#if !defined(DOUBLE_INCLUDE) && !defined(BUILDING_MONADO_ILLIXR_DRIVER)
extern "C" {
MY_EXPORT_API bool needs_monado() {
#  ifdef MONADO_REQUIRED
    return true;
#  else
    return false;
#  endif
}
}
#endif

namespace ILLIXR {

using plugin_id_t = std::size_t;

/*
 * This gets included, but it is functionally 'private'.
 */
const record_header _plugin_start_header{
    "plugin_name",
    {
        {"plugin_id", typeid(plugin_id_t)},
        {"plugin_name", typeid(std::string)},
    },
};

/**
 * @brief A dynamically-loadable plugin for Spindle.
 */
class plugin {
public:
    plugin(std::string name, phonebook* pb)
        : name_{std::move(name)}
        , phonebook_{pb}
        , record_logger_{phonebook_->lookup_impl<record_logger>()}
        , id_{pb->get_next_id()} { }

    virtual ~plugin() = default;

    /**
     * @brief A method which Spindle calls when it starts the component.
     *
     * This is necessary because a constructor can't call derived virtual
     * methods (due to structure of C++). See `threadloop` for an example of
     * this use-case.
     */
    virtual void start() {
#ifndef __ANDROID__
        record_logger_->log(record{_plugin_start_header,
                                   {
                                       {id_},
                                       {name_},
                                   }});
#endif
    }

    /**
     * @brief A method which Spindle calls when it stops the component.
     *
     * This is necessary because the parent class might define some actions that need to be
     * taken prior to destructing the derived class. For example, threadloop must halt and join
     * the thread before the derived class can be safely destructed. However, the derived
     * class's destructor is called before its parent (threadloop), so threadloop doesn't get a
     * chance to join the thread before the derived class is destroyed, and the thread accesses
     * freed memory. Instead, we call plugin->stop manually before destroying anything.
     *
     * Concrete plugins are responsible for initializing their specific logger and sinks.
     */
    virtual void stop() {
#ifndef __ANDROID__
        if (plugin_logger_)
            plugin_logger_->flush();
#endif
    }

    [[maybe_unused]] [[nodiscard]] std::string get_name() const noexcept {
        return name_;
    }

    auto spdlogger(const char* log_level) {
        if (!log_level) {
#ifdef NDEBUG
            log_level = "warn";
#else
            log_level = "debug";
#endif
        }
        std::vector<spdlog::sink_ptr> sinks;
#ifdef __ANDROID__
        auto main_sink = std::make_shared<spdlog::sinks::android_sink_mt>();
        sinks.push_back(main_sink);
#else
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/" + name_ + ".log");
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sinks.push_back(file_sink);
        sinks.push_back(console_sink);
#endif
        if (spdlog::get(name_) == nullptr) {
            plugin_logger_ = std::make_shared<spdlog::logger>(name_, begin(sinks), end(sinks));
            plugin_logger_->set_level(spdlog::level::from_str(log_level));
            spdlog::register_logger(plugin_logger_);
        } else {
            plugin_logger_ = spdlog::get(name_);
        }
        return plugin_logger_;
    }

    [[maybe_unused]] void spd_add_file_sink(const std::string& file_name, const std::string& extension,
                                            const std::string& log_level) {
        if (!plugin_logger_) {
            throw std::runtime_error("Logger not found");
        }

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/" + file_name + "." + extension, true);
        file_sink->set_level(spdlog::level::from_str(log_level));
        plugin_logger_->sinks().push_back(file_sink);
        size_t sink_count = plugin_logger_->sinks().size();
        plugin_logger_->sinks()[sink_count - 1]->set_pattern("%v");
    }

protected:
    std::string                          name_;
    const phonebook*                     phonebook_;
    const std::shared_ptr<record_logger> record_logger_;
    const std::size_t                    id_;
    std::shared_ptr<spdlog::logger>      plugin_logger_;
};

#define PLUGIN_MAIN(PLUGIN_CLASS)                                         \
    extern "C" MY_EXPORT_API plugin* this_plugin_factory(phonebook* pb) { \
        auto* obj = new PLUGIN_CLASS{#PLUGIN_CLASS, pb};                  \
        return obj;                                                       \
    }

} // namespace ILLIXR
