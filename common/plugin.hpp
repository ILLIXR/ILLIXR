#pragma once

#include "gen_guid.hpp"
#include "phonebook.hpp"

namespace ILLIXR {

using plugin_id_t = std::size_t;

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
    virtual void start() { }

    /**
     * @brief A method which Spindle calls when it stops the component.
     *
     * This is necessary because the parent class might define some actions that need to be
     * taken prior to destructing the derived class. For example, threadloop must halt and join
     * the thread before the derived class can be safely destructed. However, the derived
     * class's destructor is called before its parent (threadloop), so threadloop doesn't get a
     * chance to join the thread before the derived class is destroyed, and the thread accesses
     * freed memory. Instead, we call plugin->stop manually before destrying anything.
     */
    virtual void stop() { }

    plugin(const std::string& name_, phonebook* pb_)
        : name{name_}
        , pb{pb_}
        , gen_guid_{pb->lookup_impl<gen_guid>()}
        , id{gen_guid_->get()} { }

    virtual ~plugin() = default;

    std::string get_name() const noexcept {
        return name;
    }

protected:
    std::string                     name;
    const phonebook*                pb;
    const std::shared_ptr<gen_guid> gen_guid_;
    const plugin_id_t               id;
};

#define PLUGIN_MAIN(plugin_class)                                \
    extern "C" plugin* this_plugin_factory(phonebook* pb) {      \
        plugin_class* obj = new plugin_class{#plugin_class, pb}; \
        return obj;                                              \
    }
} // namespace ILLIXR
