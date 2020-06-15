#include <thread>
#include <chrono>
#include "common/runtime.hpp"
#include "common/extended_window.hpp"
#include "common/dynamic_lib.hpp"
#include "switchboard_impl.hpp"

using namespace ILLIXR;

class runtime_impl : public runtime {
public:
	runtime_impl(GLXContext appGLCtx) {
		pb.register_impl<switchboard>(create_switchboard());
		pb.register_impl<xlib_gl_extended_window>(std::make_shared<xlib_gl_extended_window>(448*2, 320*2, appGLCtx));
	}

	virtual void load_so(std::string_view so) {
		auto lib = dynamic_lib::create(so);
		plugin_factory this_plugin_factory = lib.get<plugin* (*) (phonebook*)>("this_plugin_factory");
		load_plugin_factory(this_plugin_factory);
		libs.push_back(std::move(lib));
	}

	void load_so_list(int argc, const char * argv[]) {
		for (int i = 1; i < argc; ++i) {
			load_so(argv[i]);
		}
	}

	virtual void load_so_list(const std::string& so_list) {
		size_t pos = 0;
		size_t prev_pos = 0;
		do {
			pos = so_list.find(':', prev_pos);
			std::string so = so_list.substr(prev_pos, pos);
			if (!so.empty()) {
				load_so(so);
			}
			if (pos == std::string::npos) {
				break;
			}
			prev_pos = pos + 1;
		} while (true);
	}

	virtual void load_plugin_factory(plugin_factory plugin_main) {
		plugins.emplace_back(plugin_main(&pb));
	}

	virtual void wait() {
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
		}
		// TODO: catch keyboard interrupt
	}
private:
	phonebook pb;
	// I have to keep the dynamic libs in scope until the program is dead
	std::vector<dynamic_lib> libs;
	std::vector<std::unique_ptr<plugin>> plugins;
};

extern "C" runtime* runtime_factory(GLXContext appGLCtx) {
	return new runtime_impl{appGLCtx};
}
