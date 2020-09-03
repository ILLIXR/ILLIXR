#pragma once

#include <vector>
#include <memory>
#include <GL/glx.h>
#include "extended_window.hpp"

namespace ILLIXR {
	class plugin;

	typedef plugin* (*plugin_factory) (phonebook*);

	class runtime {
	public:
		virtual void load_so(std::string_view so) = 0;
		virtual void load_plugin_factory(plugin_factory plugin) = 0;
		virtual void wait() = 0;
		virtual void stop() = 0;
		virtual ~runtime() {}
	};

	extern "C" runtime* runtime_factory(GLXContext appGLCtx);

}
