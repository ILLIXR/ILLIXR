#include "common/plugin.hpp"
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/phonebook.hpp"
#include "common/data_format.hpp"
#include "common/global_module_defs.hpp"

#include <iomanip>
#include <fstream>
#include <numeric>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "common/gl_util/lib/stb_image_write.h"

using namespace ILLIXR;

std::string
getenv_or(std::string var, std::string default_) {
        if (std::getenv(var.c_str())) {
                return {std::getenv(var.c_str())};
        } else {
                return default_;
        }
}

class offload_data : public threadloop {
public:
	offload_data(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _offload_data_reader{sb->subscribe_latest<texture_pose>("texture_pose")}
		, _seq_expect(1)
		, _stat_processed(0)
		, _stat_missed(0)
		, enable_offload{bool(std::stoi(getenv_or("ILLIXR_OFFLOAD_ENABLE", "0")))}
		// , obj_dir{getenv_or("ILLIXR_OFFLOAD_PATH", "metrics/offloaded_data/")}
		{
			// Remove existing file and create folder for offloading
			system(("rm -r " + obj_dir).c_str());
			system(("mkdir -p " + obj_dir).c_str());
		}

		virtual skip_option _p_should_skip() override {
			auto in = _offload_data_reader->get_latest_ro();
			if (!in || in->seq == _seq_expect-1) {
				// No new data, sleep
				std::this_thread::sleep_for(std::chrono::milliseconds{1});
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
			std::cout << "Image index: " << img_idx++ << std::endl;
			_offload_data_container.push_back(_offload_data_reader->get_latest_ro());
		}

		virtual ~offload_data() override {
			// Write offloaded data from memory to disk
			if (enable_offload)
				writeDataToDisk(_offload_data_container);
		}

private:
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<reader_latest<texture_pose>> _offload_data_reader;
	long long _seq_expect, _stat_processed, _stat_missed;
	std::vector<int> _time_seq;
	std::vector<const texture_pose*> _offload_data_container;

	int percent = 0;
	int img_idx = 0;
	bool enable_offload;
	bool is_success = false;
	std::string obj_dir = "metrics/offloaded_data/";

	void writeMetadata(std::vector<int> _time_seq)
	{
		double mean  = std::accumulate(_time_seq.begin(), _time_seq.end(), 0.0) / _time_seq.size();
		double accum = 0.0;
		std::for_each(std::begin(_time_seq), std::end(_time_seq), [&](const double d){
			accum += (d - mean) * (d - mean);
		});
		double stdev = sqrt(accum/(_time_seq.size() - 1));

		std::vector<int>::iterator max = std::max_element(_time_seq.begin(), _time_seq.end());
		std::vector<int>::iterator min = std::min_element(_time_seq.begin(), _time_seq.end());

		std::ofstream meta_file (obj_dir + "metadata.out");
		if (meta_file.is_open())
		{
			meta_file << "mean: " << mean << std::endl;
			meta_file << "max: " << *max << std::endl;
			meta_file << "min: " << *min << std::endl;
			meta_file << "stdev: " << stdev << std::endl;
			meta_file << "total number: " << _time_seq.size() << std::endl;

			meta_file << "raw time: " << std::endl;
			for (int i = 0; i < _time_seq.size(); i++)
				meta_file << _time_seq[i] << " ";
			meta_file << std::endl << std::endl << std::endl;

			meta_file << "ordered time: " << std::endl;
			std::sort(_time_seq.begin(), _time_seq.end(), [](int x, int y) {return x > y;});
			for (int i = 0; i < _time_seq.size(); i++)
				meta_file << _time_seq[i] << " ";
		}
		meta_file.close();
	}

	void writeDataToDisk(std::vector<const texture_pose*> _offload_data_container)
	{
		stbi_flip_vertically_on_write(true);

		std::cout << "Writing offloaded images to disk ... " << std::endl;
		for (int i = 0; i < _offload_data_container.size(); i++)
		{
			// Get collecting time for each frame
			_time_seq.push_back(_offload_data_container[i]->offload_time);

			std::string image_name = obj_dir + std::to_string(i) + ".png";
			std::string pose_name = obj_dir + std::to_string(i) + ".txt";

			// Write image
			is_success = stbi_write_png(image_name.c_str(), ILLIXR::FB_WIDTH, ILLIXR::FB_HEIGHT, 3, _offload_data_container[i]->image, 0);
			if (!is_success)
			{
				std::cerr << "Image create failed !!! " << std::endl;
				abort();
			}

			// Write pose
			std::ofstream pose_file (pose_name);
			if (pose_file.is_open())
			{
				std::time_t pose_time = std::chrono::system_clock::to_time_t(_offload_data_container[i]->pose_time);

				// Transfer timestamp to duration
				auto duration = (_offload_data_container[i]->pose_time).time_since_epoch().count();

				// Write time data
				pose_file << "cTime: " << std::ctime(&pose_time);
				pose_file << "strTime: " << duration << std::endl;

				// Write position coordinates in x y z
				int pose_size = _offload_data_container[i]->position.size();
				pose_file << "pos: ";
				for (int pos_idx = 0; pos_idx < pose_size; pos_idx++)
					pose_file << _offload_data_container[i]->position(pos_idx) << " ";
				pose_file << std::endl;

				// Write quaternion in w x y z
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

			// Print progress
			percent = (100 * (i + 1) / _offload_data_container.size());
			std::cout << "\r" << "[" << std::string(percent / 2, (char)61u) << std::string(100 / 2 - percent / 2, ' ') << "] ";
			std::cout << percent << "%" << " [Image " << i++ << " of " << _offload_data_container.size() << "]";
			std::cout.flush();
		}
		std::cout << std::endl;
		writeMetadata(_time_seq);
	}

};

PLUGIN_MAIN(offload_data)
