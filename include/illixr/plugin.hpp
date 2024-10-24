#pragma once
#include "phonebook.hpp"
#include "record_logger.hpp"
#include "spdlog/common.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <typeinfo>
#include <utility>

namespace ILLIXR {

using plugin_id_t = std::size_t;

/*
 * This gets included, but it is functionally 'private'. Hence the double-underscores.
 */
const record_header __plugin_start_header{
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
    /**
     * @brief A method which Spindle calls when it starts the component.
     *
     * This is necessary because a constructor can't call derived virtual
     * methods (due to structure of C++). See `threadloop` for an example of
     * this use-case.
     */
    virtual void start() {
        record_logger_->log(record{__plugin_start_header,
                                   {
                                       {id},
                                       {name},
                                   }});
    }

    /**
     * @brief A method which Spindle calls when it stops the component.
     *
     * This is necessary because the parent class might define some actions that need to be
     * taken prior to destructing the derived class. For example, threadloop must halt and join
     * the thread before the derived class can be safely destructed. However, the derived
     * class's destructor is called before its parent (threadloop), so threadloop doesn't get a
     * chance to join the thread before the derived class is destroyed, and the thread accesses
     * freed memory. Instead, we call plugin->stop manually before destrying anything.
     *
     * Concrete plugins are responsible for initializing their specific logger and sinks.
     */
    virtual void stop() { if (plugin_logger) plugin_logger->flush(); }

    plugin(std::string name_, phonebook* pb_)
        : name{std::move(name_)}
        , pb{pb_}
        , record_logger_{pb->lookup_impl<record_logger>()}
        , gen_guid_{pb->lookup_impl<gen_guid>()}
        , id{gen_guid_->get()} { }

    virtual ~plugin() = default;

    [[nodiscard]] std::string get_name() const noexcept {
        return name;
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
        auto                          file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/" + name + ".log", true);
        auto                          console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sinks.push_back(file_sink);
        sinks.push_back(console_sink);
        plugin_logger = std::make_shared<spdlog::logger>(name, begin(sinks), end(sinks));
        plugin_logger->set_level(spdlog::level::from_str(log_level));
        spdlog::register_logger(plugin_logger);
        return plugin_logger;
    }

    void spd_add_file_sink(const std::string& file_name, const std::string& extension, const std::string& log_level) {
        if (!plugin_logger) {
            throw std::runtime_error("Logger not found");
        }

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/" + file_name + "." + extension, true);
        file_sink->set_level(spdlog::level::from_str(log_level));
        plugin_logger->sinks().push_back(file_sink);
        size_t sink_count = plugin_logger->sinks().size();
        plugin_logger->sinks()[sink_count-1]->set_pattern("%v");
    }

protected:
    std::string                          name;
    const phonebook*                     pb;
    const std::shared_ptr<record_logger> record_logger_;
    const std::shared_ptr<gen_guid>      gen_guid_;
    const std::size_t                    id;
    std::shared_ptr<spdlog::logger>      plugin_logger;
};

#define PLUGIN_MAIN(plugin_class)                           \
    extern "C" plugin* this_plugin_factory(phonebook* pb) { \
        auto* obj = new plugin_class{#plugin_class, pb};    \
        return obj;                                         \
    }
} // namespace ILLIXR
