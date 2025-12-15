#pragma once

#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"

#include <string>
#include <vector>
#ifdef ILLIXR_ANDROID_BUILD
#include <EGL/egl.h>
#endif

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

#if defined(ILLIXR_ANDROID_BUILD) && !defined(ENABLE_MONADO)
extern "C" runtime* runtime_factory(EGLContext appGLCtx, ANativeWindow *window);
#else
extern "C" runtime* runtime_factory();
#endif

} // namespace ILLIXR
