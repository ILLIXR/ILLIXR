#include <memory>
#include <random>
#include <chrono>
#include <future>
#include <thread>
#include "common/component.hh"
#include "switchboard_impl.hh"
#include "dynamic_lib.hh"

using namespace ILLIXR;

int main(int argc, char** argv) {
	/* TODO: use a config-file instead of cmd-line args. Config file
	   can be more complex and can be distributed more easily (checked
	   into git repository). */

	auto sb = create_switchboard();

	// I have to keep the dynamic libs in scope until the program is dead
	std::vector<dynamic_lib> libs;
	std::vector<std::unique_ptr<component>> components;
	for (int i = 1; i < argc; ++i) {
		auto lib = dynamic_lib::create(std::string_view{argv[i]});
		auto comp = std::unique_ptr<component>(lib.get<create_component_fn>("create_component")(sb.get()));
		comp->start();
		libs.push_back(std::move(lib));
		components.push_back(std::move(comp));
	}

	auto t = std::thread([&]() {
		std::default_random_engine generator;
		std::uniform_int_distribution<int> distribution{200, 600};

		std::cout << "Model an XR app by calling for a pose sporadically."
				  << std::endl;

		auto pose_sub = sb->subscribe_latest<pose_type>("pose");

		for (int i = 0; i < 200; ++i) {
			int delay = distribution(generator);
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			auto cur_pose = pose_sub->get_latest_ro();

			// If there is no writer, cur_pose might be null
			if (cur_pose) {
				auto r = cur_pose->position;
				std::cout << "position = [" << r[0] << ", " << r[1] << ", " << r[2] << "]" << std::endl;
			} else {
				std::cout << "No cur_pose published yet" << std::endl;
			}
		}

	});

	t.join();

	for (auto&& comp : components) {
		comp->stop();
	}

	return 0;
}
