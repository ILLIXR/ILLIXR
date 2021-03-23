#include "common/plugin.hpp"
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/phonebook.hpp"
#include "common/data_format.hpp"
#include "common/global_module_defs.hpp"
#include "common/error_util.hpp"

#include <iomanip>
#include <fstream>
#include <numeric>
#include <boost/filesystem.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
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
		, percent{0}
		, img_idx{0}
		, enable_offload{ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_OFFLOAD_ENABLE", "False"))}
		, is_success{true}
		/// TODO: Set with #198
		, obj_dir{ILLIXR::getenv_or("ILLIXR_OFFLOAD_PATH", "metrics/offloaded_data/")}
	{ }

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
#ifndef NDEBUG
		std::cout << "Image index: " << img_idx++ << std::endl;
#endif
		_offload_data_container.push_back(_offload_data_reader->get_latest_ro());
	}

	virtual ~offload_data() override {
		// Write offloaded data from memory to disk
		if (enable_offload)
		{
			boost::filesystem::path p(obj_dir);
			boost::filesystem::remove_all(p);
			boost::filesystem::create_directories(p);

			writeDataToDisk(_offload_data_container);
		}
	}

private:
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<reader_latest<texture_pose>> _offload_data_reader;
	long long _seq_expect, _stat_processed, _stat_missed;
	std::vector<int> _time_seq;
	std::vector<const texture_pose*> _offload_data_container;

	int percent;
	int img_idx;
	bool enable_offload;
	bool is_success;
	std::string obj_dir;

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
			for (int& it : _time_seq)
				meta_file << it << " ";
			meta_file << std::endl << std::endl << std::endl;

			meta_file << "ordered time: " << std::endl;
			std::sort(_time_seq.begin(), _time_seq.end(), [](int x, int y) {return x > y;});
			for (int& it : _time_seq)
				meta_file << it << " ";
		}
		meta_file.close();
	}

	void writeDataToDisk(std::vector<const texture_pose*> _offload_data_container)
	{
		stbi_flip_vertically_on_write(true);

		std::cout << "Writing offloaded images to disk ... " << std::endl;
		img_idx = 0;
		for (auto& container_it : _offload_data_container)
		{
			// Get collecting time for each frame
			_time_seq.push_back(container_it->offload_time);

			std::string image_name = obj_dir + std::to_string(img_idx) + ".png";
			std::string pose_name = obj_dir + std::to_string(img_idx) + ".txt";

			// Write image
			is_success = stbi_write_png(image_name.c_str(), ILLIXR::FB_WIDTH, ILLIXR::FB_HEIGHT, 3, container_it->image, 0);
			if (!is_success)
			{
                ILLIXR::abort("Image create failed !!! ");
			}

			// Write pose
			std::ofstream pose_file (pose_name);
			if (pose_file.is_open())
			{
				std::time_t pose_time = std::chrono::system_clock::to_time_t(container_it->pose_time);

				// Transfer timestamp to duration
				auto duration = (container_it->pose_time).time_since_epoch().count();

				// Write time data
				pose_file << "cTime: " << std::ctime(&pose_time);
				pose_file << "strTime: " << duration << std::endl;

				// Write position coordinates in x y z
				int pose_size = container_it->position.size();
				pose_file << "pos: ";
				for (int pos_idx = 0; pos_idx < pose_size; pos_idx++)
					pose_file << container_it->position(pos_idx) << " ";
				pose_file << std::endl;

				// Write quaternion in w x y z
				pose_file << "latest_pose_orientation: ";
				pose_file << container_it->latest_quaternion.w() << " ";
				pose_file << container_it->latest_quaternion.x() << " ";
				pose_file << container_it->latest_quaternion.y() << " ";
				pose_file << container_it->latest_quaternion.z() << std::endl;

				pose_file << "render_pose_orientation: ";
				pose_file << container_it->render_quaternion.w() << " ";
				pose_file << container_it->render_quaternion.x() << " ";
				pose_file << container_it->render_quaternion.y() << " ";
				pose_file << container_it->render_quaternion.z();
			}
			pose_file.close();

			// Print progress
			percent = (100 * (img_idx + 1) / _offload_data_container.size());
			std::cout << "\r" << "[" << std::string(percent / 2, (char)61u) << std::string(100 / 2 - percent / 2, ' ') << "] ";
			std::cout << percent << "%" << " [Image " << img_idx++ << " of " << _offload_data_container.size() << "]";
			std::cout.flush();
		}
		std::cout << std::endl;
		writeMetadata(_time_seq);
	}

};

PLUGIN_MAIN(offload_data)
