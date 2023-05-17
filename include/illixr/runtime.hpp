#pragma once

#include "extended_window.hpp"

#include <GL/glx.h>
#include <memory>
#include <vector>

namespace ILLIXR {
class plugin;

typedef plugin* (*plugin_factory)(phonebook*);

class runtime {
public:
    virtual void load_so(const std::vector<std::string>& so) = 0;
    virtual void load_so(const std::string_view so)          = 0;
    virtual void load_plugin_factory(plugin_factory plugin)  = 0;

    /**
     * Returns when the runtime is completely stopped.
     */
    virtual void wait() = 0;

    /**
     * Requests that the runtime is completely stopped.
     * Clients must call this before deleting the runtime.
     */
    virtual void stop() = 0;

    virtual ~runtime() = default;
};

#ifdef ILLIXR_MONADO_MAINLINE
extern "C" runtime* runtime_factory();
#else
extern "C" runtime* runtime_factory(GLXContext appGLCtx);
#endif /// ILLIXR_MONADO_MAINLINE

} // namespace ILLIXR
