#pragma once

#include "illixr/export.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"

#include <string>
#include <vector>

namespace ILLIXR {
class plugin;

typedef plugin* (*plugin_factory)(phonebook*);

class runtime {
public:
    virtual void                  load_so(const std::vector<std::string>& so) = 0;
    [[maybe_unused]] virtual void load_so(const std::string_view& so)         = 0;
    virtual void                  load_plugin_factory(plugin_factory plugin)  = 0;

    /**
     * Returns when the runtime is completely stopped.
     */
    virtual void wait() = 0;

    /**
     * Requests that the runtime is completely stopped.
     * Clients must call this before deleting the runtime.
     */
    virtual void _stop() = 0;

    void stop() {
        _stop();
    }

    std::shared_ptr<switchboard> get_switchboard() {
        return switchboard_;
    }

    virtual ~runtime() = default;

protected:
    bool                         enable_monado_ = false;
    std::shared_ptr<switchboard> switchboard_;
};

extern "C" MY_EXPORT_API runtime* runtime_factory();

} // namespace ILLIXR
