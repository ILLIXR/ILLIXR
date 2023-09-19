#include "illixr/plugin.hpp"

#include "illixr/data_format.hpp"
#include "illixr/error_util.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"

#include <boost/filesystem.hpp>
#include <iostream>
#include <numeric>
#include <utility>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "illixr/gl_util/lib/stb_image_write.h"

using namespace ILLIXR;

class offload_data : public plugin {
public:
    offload_data(std::string name_, phonebook* pb_)
        : plugin{std::move(name_), pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , percent{0}
        , img_idx{0}
        , enable_offload{ILLIXR::str_to_bool(ILLIXR::getenv_or("ILLIXR_OFFLOAD_ENABLE", "False"))}
        , is_success{true} /// TODO: Set with #198
        , obj_dir{ILLIXR::getenv_or("ILLIXR_OFFLOAD_PATH", "metrics/offloaded_data/")} {
        spdlogger(std::getenv("OFFLOAD_DATA_LOG_LEVEL"));
        sb->schedule<texture_pose>(id, "texture_pose", [&](const switchboard::ptr<const texture_pose>& datum, size_t) {
            callback(datum);
        });
    }

    void callback(const switchboard::ptr<const texture_pose>& datum) {
#ifndef NDEBUG
        spdlog::get(name)->debug("Image index: {}", img_idx++);
#endif
        /// A texture pose is present. Store it back to our container.
        _offload_data_container.push_back(datum);
    }

    ~offload_data() override {
        // Write offloaded data from memory to disk
        if (enable_offload) {
            boost::filesystem::path p(obj_dir);
            boost::filesystem::remove_all(p);
            boost::filesystem::create_directories(p);

            writeDataToDisk();
        }
    }

private:
    const std::shared_ptr<switchboard>                sb;
    std::vector<long>                                 _time_seq;
    std::vector<switchboard::ptr<const texture_pose>> _offload_data_container;

    int         percent;
    int         img_idx;
    bool        enable_offload;
    bool        is_success;
    std::string obj_dir;

    void writeMetadata() {
        double mean  = std::accumulate(_time_seq.begin(), _time_seq.end(), 0.0) / static_cast<double>(_time_seq.size());
        double accum = 0.0;
        std::for_each(std::begin(_time_seq), std::end(_time_seq), [&](const double d) {
            accum += (d - mean) * (d - mean);
        });
        double stdev = sqrt(accum / static_cast<double>((_time_seq.size() - 1)));

        auto max = std::max_element(_time_seq.begin(), _time_seq.end());
        auto min = std::min_element(_time_seq.begin(), _time_seq.end());

        std::ofstream meta_file(obj_dir + "metadata.out");
        if (meta_file.is_open()) {
            meta_file << "mean: " << mean << std::endl;
            meta_file << "max: " << *max << std::endl;
            meta_file << "min: " << *min << std::endl;
            meta_file << "stdev: " << stdev << std::endl;
            meta_file << "total number: " << _time_seq.size() << std::endl;

            meta_file << "raw time: " << std::endl;
            for (long& it : _time_seq)
                meta_file << it << " ";
            meta_file << std::endl << std::endl << std::endl;

            meta_file << "ordered time: " << std::endl;
            std::sort(_time_seq.begin(), _time_seq.end(), [](int x, int y) {
                return x > y;
            });
            for (long& it : _time_seq)
                meta_file << it << " ";
        }
        meta_file.close();
    }

    void writeDataToDisk() {
        stbi_flip_vertically_on_write(true);

        spdlog::get(name)->info("Writing offloaded images to disk...");
        img_idx = 0;
        for (auto& container_it : _offload_data_container) {
            // Get collecting time for each frame
            _time_seq.push_back(
                std::chrono::duration_cast<std::chrono::duration<long, std::milli>>(container_it->offload_duration).count());

            std::string image_name = obj_dir + std::to_string(img_idx) + ".png";
            std::string pose_name  = obj_dir + std::to_string(img_idx) + ".txt";

            // Write image
            is_success = stbi_write_png(image_name.c_str(), display_params::width_pixels, display_params::height_pixels, 3,
                                        container_it->image, 0);
            if (!is_success) {
                ILLIXR::abort("Image create failed !!! ");
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
            percent = static_cast<int>(100 * (img_idx + 1) / _offload_data_container.size());
            std::cout << "\r"
                      << "[" << std::string(percent / 2, (char) 61u) << std::string(100 / 2 - percent / 2, ' ') << "] ";
            std::cout << percent << "%"
                      << " [Image " << img_idx++ << " of " << _offload_data_container.size() << "]";
            std::cout.flush();
        }
        std::cout << std::endl;
        writeMetadata();
    }
};

PLUGIN_MAIN(offload_data)
