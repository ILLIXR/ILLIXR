#include <vector>
#include <GL/glew.h>
#include <GL/glx.h>
#include <memory>
#include <random>
#include <chrono>
#include <future>
#include <thread>
#include "common/plugin.hpp"
#include "common/data_format.hpp"
#include "common/extended_window.hpp"
#include "switchboard_impl.hpp"
#include "common/dynamic_lib.hpp"
#include "common/logging.hpp"

using namespace ILLIXR;

phonebook pb;
// I have to keep the dynamic libs in scope until the program is dead
std::vector<dynamic_lib> libs;
std::vector<std::unique_ptr<plugin>> plugins;

extern "C" int illixrrt_init(void* appGLCtx) {
	pb.register_impl<switchboard>(create_switchboard());
	pb.register_impl<c_logger>(std::make_shared<c_logger>());
	pb.register_impl<c_gen_guid>(std::make_shared<c_gen_guid>());
	pb.register_impl<xlib_gl_extended_window>(std::make_shared<xlib_gl_extended_window>(448*2, 320*2, (GLXContext)appGLCtx));
	// pb->register_impl<global_config>(new global_config {headless_window});
	return 0;
}

extern "C" void illixrrt_load_plugin(const char *path) {
	auto lib = dynamic_lib::create(std::string_view{path});
	plugin* p = lib.get<plugin* (*) (phonebook*)>("plugin_factory")(&pb);
	plugins.emplace_back(p);
	libs.push_back(std::move(lib));
}

extern "C" void illixrrt_attach_plugin(plugin* (*f) (phonebook*)) {
	plugin* p = f(&pb);
	plugins.emplace_back(p);
}

int main(int argc, char **argv) {
	if (illixrrt_init(NULL) != 0) {
		return 1;
	}
	for (int i = 1; i < argc; ++i) {
		illixrrt_load_plugin(argv[i]);
	}
	while (true) { }
	return 0;
}
