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
#include "sqlite_logger.hpp"
#include "common/dynamic_lib.hpp"
#include "common/logging.hpp"

using namespace ILLIXR;

phonebook pb;
// I have to keep the dynamic libs in scope until the program is dead
std::vector<dynamic_lib> libs;
std::vector<std::unique_ptr<plugin>> plugins;

class stdout_metric_logger : public c_metric_logger {
protected:
	virtual void log2(const struct_type* ty, std::unique_ptr<const record>&& r_) override {
		const char* r = reinterpret_cast<const char*>(r_.get());
		std::cout << "record:" << ty->name << ",";
		for (const auto& pair : ty->fields) {
			const std::string& name = pair.first;
			const type* type_ = pair.second;
			std::cout << name << ":";
			if (false) {
			} else if (type_->type_id == types::std__size_t.type_id) {
				std::cout << *reinterpret_cast<const std::size_t*>(r) << ',';
			} else if (type_->type_id == types::std__string.type_id) {
				std::cout << "\"" << *reinterpret_cast<const std::string*>(r) << "\",";
			} else if (type_->type_id == types::std__chrono__nanoseconds.type_id) {
				std::cout << reinterpret_cast<const std::chrono::nanoseconds*>(r)->count() << "ns,";
			} else {
				std::cout << "type(" << type_->name << "),";
			}
			r += type_->size;
		}
		std::cout << "\n";
	}
	virtual void log_many2(const struct_type* ty, std::vector<std::unique_ptr<const record>>&& rs) override {
		for (std::unique_ptr<const record>& r : rs) {
			log2(ty, std::move(r));
		}
	}
};

extern "C" int illixrrt_init(void* appGLCtx) {
	pb.register_impl<c_metric_logger>(std::make_shared<sqlite_metric_logger>());
	pb.register_impl<switchboard>(create_switchboard());
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
