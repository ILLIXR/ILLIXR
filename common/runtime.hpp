#include <vector>
#include <memory>
#include <GL/glx.h>
#include "plugin.hpp"

namespace ILLIXR {

typedef plugin* (*plugin_factory) (phonebook*);

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

class runtime {
public:
	virtual void load_so(std::string_view so) = 0;
	virtual void load_so_list(const std::string& path) = 0;
	virtual void load_so_list(int argc, const char * argv[]) = 0;
	virtual void load_plugin_factory(plugin_factory plugin) = 0;
	virtual void wait() = 0;
};

extern "C" runtime* runtime_factory(GLXContext appGLCtx);

}
