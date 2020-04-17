#include <GL/glew.h>
#include <GL/glx.h>
#include <memory>
#include <random>
#include <chrono>
#include <future>
#include <thread>
#include "common/component.hh"
#include "switchboard_impl.hh"
#include "dynamic_lib.hh"

using namespace ILLIXR;

static constexpr int   EYE_TEXTURE_WIDTH   = 1024;
static constexpr int   EYE_TEXTURE_HEIGHT  = 1024;


// Temporary OpenGL-specific code for creating shared OpenGL context.
// May be superceded in the future by more modular, Vulkan-based resource management.

std::unique_ptr<switchboard> sb;
// I have to keep the dynamic libs in scope until the program is dead
std::vector<dynamic_lib> libs;
std::vector<std::unique_ptr<component>> components;
std::thread t;

extern "C" int illixrrt_init(void* appGLCtx) {
	/* TODO: use a config-file instead of cmd-line args. Config file
	   can be more complex and can be distributed more easily (checked
	   into git repository). */

	sb = create_switchboard();

	// Grab a writer object and declare that we're publishing to the "global_config" topic, used to provide
	// components with global-scope general configuration information.
	std::cout << "Main is publishing global config data to Switchboard" << std::endl;
	std::unique_ptr<writer<global_config>> _m_config = sb->publish<global_config>("global_config");

	auto config = new global_config;
	// trust me, this is a GLXContext
    config->glctx = appGLCtx;
	_m_config->put(config);

	return 0;
}

extern "C" void illixrrt_load_component(const char *path) {
	auto lib = dynamic_lib::create(std::string_view{path});
	auto comp = std::unique_ptr<component>(lib.get<create_component_fn>("create_component")(sb.get()));
	comp->start();
	libs.push_back(std::move(lib));
	components.push_back(std::move(comp));
}

extern "C" void illixrrt_attach_component(create_component_fn f) {
	auto comp = std::unique_ptr<component>(f(sb.get()));
	comp->start();
	components.push_back(std::move(comp));
}

// TODO: deleted main runtime thread. Rethink whether run and join is necessary
extern "C" void illixrrt_run() {
}

extern "C" void illixrrt_join() {
}

extern "C" void illixrrt_destroy() {
	for (auto&& comp : components) {
		comp->stop();
	}
}

int main(int argc, char **argv) {
	if (illixrrt_init(NULL) != 0) {
		return 1;
	}
	for (int i = 1; i < argc; ++i) {
		illixrrt_load_component(argv[i]);
	}
	illixrrt_run();
	illixrrt_join();
	for (;;) {
	}
	illixrrt_destroy();
	return 0;
}
