#include "plugin.hpp"

#include <boost/filesystem.hpp>
#include <fstream>
#include <numeric>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "illixr/gl_util/lib/stb_image_write.h"

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] offload_data::offload_data(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , percent_{0}
    , img_idx_{0}
    , enable_offload_{ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_OFFLOAD_ENABLE", "False"))}
    , is_success_{true} /// TODO: Set with #198
    , obj_dir_{ILLIXR::getenv_or("ILLIXR_OFFLOAD_PATH", "metrics/offloaded_data/")} {
    spdlogger(std::getenv("OFFLOAD_DATA_LOG_LEVEL"));
    switchboard_->schedule<texture_pose>(id_, "texture_pose", [&](const switchboard::ptr<const texture_pose>& datum, size_t) {
        callback(datum);
    });
}

void offload_data::callback(const switchboard::ptr<const texture_pose>& datum) {
    spdlog::get(name_)->debug("Image index: {}", img_idx_++);
    /// A texture pose is present. Store it back to our container.
    offload_data_container_.push_back(datum);
}

offload_data::~offload_data() {
    // Write offloaded data from memory to disk
    if (enable_offload_) {
        boost::filesystem::path p(obj_dir_);
        boost::filesystem::remove_all(p);
        boost::filesystem::create_directories(p);

        write_data_to_disk();
    }
}

void offload_data::write_metadata() {
    double mean  = std::accumulate(time_seq_.begin(), time_seq_.end(), 0.0) / static_cast<double>(time_seq_.size());
    double accum = 0.0;
    std::for_each(std::begin(time_seq_), std::end(time_seq_), [&](const double d) {
        accum += (d - mean) * (d - mean);
    });
    double stdev = sqrt(accum / static_cast<double>((time_seq_.size() - 1)));

    auto max = std::max_element(time_seq_.begin(), time_seq_.end());
    auto min = std::min_element(time_seq_.begin(), time_seq_.end());

    std::ofstream meta_file(obj_dir_ + "metadata.out");
    if (meta_file.is_open()) {
        meta_file << "mean: " << mean << std::endl;
        meta_file << "max: " << *max << std::endl;
        meta_file << "min: " << *min << std::endl;
        meta_file << "stdev: " << stdev << std::endl;
        meta_file << "total number: " << time_seq_.size() << std::endl;

        meta_file << "raw time: " << std::endl;
        for (long& it : time_seq_)
            meta_file << it << " ";
        meta_file << std::endl << std::endl << std::endl;

        meta_file << "ordered time: " << std::endl;
        std::sort(time_seq_.begin(), time_seq_.end(), [](int x, int y) {
            return x > y;
        });
        for (long& it : time_seq_)
            meta_file << it << " ";
    }
    meta_file.close();
}

void offload_data::write_data_to_disk() {
    stbi_flip_vertically_on_write(true);

    spdlog::get(name_)->info("Writing offloaded images to disk...");
    img_idx_ = 0;
    for (auto& container_it : offload_data_container_) {
        // Get collecting time for each frame
        time_seq_.push_back(
            std::chrono::duration_cast<std::chrono::duration<long, std::milli>>(container_it->offload_duration).count());

        std::string image_name = obj_dir_ + std::to_string(img_idx_) + ".png";
        std::string pose_name  = obj_dir_ + std::to_string(img_idx_) + ".txt";

        // Write image
        is_success_ = stbi_write_png(image_name.c_str(), display_params::width_pixels, display_params::height_pixels, 3,
                                     container_it->image, 0);
        if (!is_success_) {
            throw std::runtime_error("Image create failed !!! ");
        }

        // Write pose
        std::ofstream pose_file(pose_name);
        if (pose_file.is_open()) {
            // Transfer timestamp to duration
            auto duration = (container_it->pose_time).time_since_epoch().count();

            // Write time data
            pose_file << "strTime: " << duration << std::endl;

            // Write position coordinates in x y z
            int pose_size = static_cast<int>(container_it->position.size());
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
        percent_ = static_cast<int>(100 * (img_idx_ + 1) / offload_data_container_.size());
        std::cout << "\r"
                  << "[" << std::string(percent_ / 2, (char) 61u) << std::string(100 / 2 - percent_ / 2, ' ') << "] ";
        std::cout << percent_ << "%"
                  << " [Image " << img_idx_++ << " of " << offload_data_container_.size() << "]";
        std::cout.flush();
    }
    std::cout << std::endl;
    write_metadata();
}

PLUGIN_MAIN(offload_data)
