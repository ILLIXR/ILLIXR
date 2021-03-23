#include <cmath>
#include <shared_mutex>
#include "common/phonebook.hpp"
#include "common/pose_prediction.hpp"
#include "common/data_format.hpp"
#include "common/plugin.hpp"
#include "common/global_module_defs.hpp"


#include "utils.hpp"
#include "data_loading.hpp"

using namespace ILLIXR;


class pose_lookup_impl : public pose_prediction {
public:
    pose_lookup_impl(const phonebook* const pb)
        : sb{pb->lookup_impl<switchboard>()}
        , _m_sensor_data{load_data()}
        , dataset_first_time{_m_sensor_data.cbegin()->first}
        , _m_start_of_time{std::chrono::high_resolution_clock::now()}
        , _m_vsync_estimate{sb->subscribe_latest<time_type>("vsync_estimate")}
        /// TODO: Set with #198
        , enable_alignment{ILLIXR::str_to_bool(getenv_or("ILLIXR_ALIGNMENT_ENABLE", "False"))}
        , init_pos_offset{0}
        , align_rot{Eigen::Matrix3f::Zero()}
        , align_trans{0}
        , align_quat{0}
        , align_scale{0.0}
        , path_to_alignment{ILLIXR::getenv_or("ILLIXR_ALIGNMENT_FILE", "./metrics/alignMatrix.txt")}
    {
        if (enable_alignment)
            load_align_parameters(path_to_alignment, align_rot, align_trans, align_quat, align_scale);
        // Read position data of the first frame
        init_pos_offset = _m_sensor_data.cbegin()->second.position;

        auto newoffset = correct_pose(_m_sensor_data.begin()->second).orientation;
        set_offset(newoffset);
    }

    virtual fast_pose_type get_fast_pose() const override {
        const time_type* estimated_vsync = _m_vsync_estimate->get_latest_ro();
        if(estimated_vsync == nullptr) {
            std::cerr << "Vsync estimation not valid yet, returning fast_pose for now()" << std::endl;
            return get_fast_pose(std::chrono::system_clock::now());
        } else {
            return get_fast_pose(*estimated_vsync);
        }
    }

    virtual pose_type get_true_pose() const override {
        throw std::logic_error{"Not Implemented"};
    }

    virtual bool fast_pose_reliable() const override {
        return true;
    }

    virtual bool true_pose_reliable() const override {
        return false;
    }

    virtual Eigen::Quaternionf get_offset() override {
        return offset;
    }

    virtual pose_type correct_pose(const pose_type pose) const override {
        pose_type swapped_pose;

        // Step 1: Compensate starting point to (0, 0, 0), pos only
        auto input_pose = pose_type{
            .sensor_time = pose.sensor_time,
            .position = Eigen::Vector3f{
                pose.position(0) - init_pos_offset(0),
                pose.position(1) - init_pos_offset(1),
                pose.position(2) - init_pos_offset(2),
            },
            .orientation = pose.orientation,
        };

        if (enable_alignment)
        {
            // Step 2: Apply estimated alignment parameters
            // Step 2.1: Position alignment
            input_pose.position = align_scale * align_rot * input_pose.position + align_trans;

            // Step 2.2: Orientation alignment
            Eigen::Vector4f quat_in = {pose.orientation.x(), pose.orientation.y(), pose.orientation.z(), pose.orientation.w()};
            Eigen::Vector4f quat_out = ori_multiply(quat_in, ori_inv(align_quat));
            input_pose.orientation.x() = quat_out(0);
            input_pose.orientation.y() = quat_out(1);
            input_pose.orientation.z() = quat_out(2);
            input_pose.orientation.w() = quat_out(3);
        }

        // Step 3: Swap axis for both position and orientation
        // Step 3.1: Swap for position
        // This uses the OpenVINS standard output coordinate system.
        // This is a mapping between the OV coordinate system and the OpenGL system.
        swapped_pose.position.x() = -input_pose.position.y();
        swapped_pose.position.y() = input_pose.position.z();
        swapped_pose.position.z() = -input_pose.position.x();

        // Step 3.2: Swap for orientation
        // There is a slight issue with the orientations: basically,
        // the output orientation acts as though the "top of the head" is the
        // forward direction, and the "eye direction" is the up direction.
        Eigen::Quaternionf raw_o (input_pose.orientation.w(), -input_pose.orientation.y(), input_pose.orientation.z(), -input_pose.orientation.x());

        swapped_pose.orientation = apply_offset(raw_o);

        return swapped_pose;
    }

    virtual void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override{
        std::unique_lock lock {offset_mutex};
        Eigen::Quaternionf raw_o = raw_o_times_offset * offset.inverse();
        //std::cout << "pose_prediction: set_offset" << std::endl;
        offset = raw_o.inverse();
    }

    Eigen::Quaternionf apply_offset(const Eigen::Quaternionf& orientation) const {
        std::shared_lock lock {offset_mutex};
        return orientation * offset;
    }

    virtual fast_pose_type get_fast_pose(time_type time) const override {
        ullong lookup_time = std::chrono::nanoseconds(time - _m_start_of_time).count() + dataset_first_time;

        auto nearest_row = _m_sensor_data.upper_bound(lookup_time);

        if (nearest_row == _m_sensor_data.cend()) {
#ifndef NDEBUG
            std::cerr << "Time " << lookup_time << " (" << std::chrono::nanoseconds(time - _m_start_of_time).count() << " + " << dataset_first_time << ") after last datum " << _m_sensor_data.rbegin()->first << std::endl;
#endif
            nearest_row--;
        } else if (nearest_row == _m_sensor_data.cbegin()) {
#ifndef NDEBUG
            std::cerr << "Time " << lookup_time << " (" << std::chrono::nanoseconds(time - _m_start_of_time).count() << " + " << dataset_first_time << ") before first datum " << _m_sensor_data.cbegin()->first << std::endl;
#endif
        } else {
            // "std::map::upper_bound" returns an iterator to the first pair whose key is GREATER than the argument.
            // I already know we aren't at the begin()
            // So I will decrement nearest_row here.
            nearest_row--;
        }

        auto looked_up_pose = nearest_row->second;
        looked_up_pose.sensor_time = _m_start_of_time + std::chrono::nanoseconds{nearest_row->first - dataset_first_time};
        return fast_pose_type{
            .pose = correct_pose(looked_up_pose),
            .predict_computed_time = std::chrono::system_clock::now(),
            .predict_target_time = time
        };

    }

private:
    const std::shared_ptr<switchboard> sb;
    mutable Eigen::Quaternionf offset {Eigen::Quaternionf::Identity()};
    mutable std::shared_mutex offset_mutex;

    const std::map<ullong, sensor_types> _m_sensor_data;
    ullong dataset_first_time;
    time_type _m_start_of_time;
    std::unique_ptr<reader_latest<time_type>> _m_vsync_estimate;

    bool enable_alignment;
    Eigen::Vector3f init_pos_offset;
    Eigen::Matrix3f align_rot;
    Eigen::Vector3f align_trans;
    Eigen::Vector4f align_quat;
    double align_scale;
    std::string path_to_alignment;
};

class pose_lookup_plugin : public plugin {
public:
    pose_lookup_plugin(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    {
        pb->register_impl<pose_prediction>(
            std::static_pointer_cast<pose_prediction>(
                std::make_shared<pose_lookup_impl>(pb)
            )
        );
    }
};

PLUGIN_MAIN(pose_lookup_plugin);
