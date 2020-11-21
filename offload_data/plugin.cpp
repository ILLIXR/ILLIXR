#include "common/plugin.hpp"
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/phonebook.hpp"
#include "common/data_format.hpp"

#include <iomanip>
#include <fstream>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "common/gl_util/lib/stb_image_write.h"

using namespace ILLIXR;

class offload_data : public threadloop {
public:
	offload_data(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _offload_data_reader{sb->subscribe_latest<texture_pose>("texture_pose")}
		, _seq_expect(1)
		, _stat_processed(0)
		, _stat_missed(0)
		{
			obj_dir = std::getenv("ILLIXR_OUTPUT_DATA");
			if(obj_dir == NULL) {
					std::cerr << "Please define ILLIXR_OUTPUT_DATA." << std::endl;
					abort();
			}
			// remove existing file and create folder for offloading
			system(("rm -r " + std::string(obj_dir)).c_str());
			system(("mkdir -p " + std::string(obj_dir)).c_str());
		}

		virtual skip_option _p_should_skip() override {
			auto in = _offload_data_reader->get_latest_ro();
			if (!in || in->seq == _seq_expect-1) {
				// No new data, sleep
				return skip_option::skip_and_yield;
			} else {
				if (in->seq != _seq_expect) {
					_stat_missed = in->seq - _seq_expect;
				} else {
					_stat_missed = 0;
				}
				_stat_processed++;
				_seq_expect = in->seq+1;
				return skip_option::run;
			}
		}

		void _p_one_iteration() override {
			std::cout << img_idx++ << std::endl;
			_offload_data_container.push_back(_offload_data_reader->get_latest_ro());
		}

		virtual ~offload_data() override {
			std::cout << "total vector size: " << _offload_data_container.size() << std::endl;

			stbi_flip_vertically_on_write(true);

			for (int i = 0; i < _offload_data_container.size(); i++)
			{
				std::cout << "idx: " << std::setw(4) << i
									 << " R: " << std::setw(3) << (int)((_offload_data_container[i]->image)[1])
									 << " G: " << std::setw(3) << (int)((_offload_data_container[i]->image)[2])
									 << " B: " << std::setw(3) << (int)((_offload_data_container[i]->image)[3])
									 << std::endl;
				std::string image_name = obj_dir + std::to_string(i) + ".png";
				std::string pose_name = obj_dir + std::to_string(i) + ".txt";

				// write image
				is_success = stbi_write_png(image_name.c_str(), 1024, 1024, 3, _offload_data_container[i]->image, 0);
				if (!is_success)
					std::cout << "image create failed!!!" << std::endl;

				// write pose
				std::ofstream pose_file (pose_name);
				if (pose_file.is_open())
				{
					std::time_t pose_time = std::chrono::system_clock::to_time_t(_offload_data_container[i]->pose_time);

					// transfer timestamp to duration
					auto duration = (_offload_data_container[i]->pose_time).time_since_epoch().count();

					// write time data
					pose_file << "cTime: " << std::ctime(&pose_time);
					pose_file << "strTime: " << duration << std::endl;

					// write position coordinates in x y z
					int pose_size = _offload_data_container[i]->position.size();
					pose_file << "pos: ";
					for (int pos_idx = 0; pos_idx < pose_size; pos_idx++)
						pose_file << _offload_data_container[i]->position(pos_idx) << " ";
					pose_file << std::endl;

					// write quaternion in w x y z
					pose_file << "latest_pose_orientation: ";
					pose_file << _offload_data_container[i]->latest_quaternion.w() << " ";
					pose_file << _offload_data_container[i]->latest_quaternion.x() << " ";
					pose_file << _offload_data_container[i]->latest_quaternion.y() << " ";
					pose_file << _offload_data_container[i]->latest_quaternion.z() << std::endl;

					pose_file << "render_pose_orientation: ";
					pose_file << _offload_data_container[i]->render_quaternion.w() << " ";
					pose_file << _offload_data_container[i]->render_quaternion.x() << " ";
					pose_file << _offload_data_container[i]->render_quaternion.y() << " ";
					pose_file << _offload_data_container[i]->render_quaternion.z();
				}
				pose_file.close();
			}
		}

private:
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<reader_latest<texture_pose>> _offload_data_reader;
	long long _seq_expect, _stat_processed, _stat_missed;
	std::vector<const texture_pose*> _offload_data_container;

	int img_idx = 0;
	char* obj_dir = NULL;
	bool is_success = false;
};

PLUGIN_MAIN(offload_data)
