#include "service.hpp"

#include "illixr/iterators/csv_iterator.hpp"
#include "utils.hpp"

#include <memory>
#include <shared_mutex>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

inline std::map<ullong, pose_type> read_data(std::ifstream& gt_file, const std::string& file_name) {
    (void) file_name;
    std::map<ullong, pose_type> data;

    for (csv_iterator row{gt_file, 1}; row != csv_iterator{}; ++row) {
        ullong             t = std::stoull(row[0]);
        Eigen::Vector3f    av{std::stof(row[1]), std::stof(row[2]), std::stof(row[3])};
        Eigen::Quaternionf la{std::stof(row[4]), std::stof(row[5]), std::stof(row[6]), std::stof(row[7])};
        data[t] = {{}, av, la};
    }
    return data;
}

pose_lookup_impl::pose_lookup_impl(const phonebook* const pb)
    : switchboard_{pb->lookup_impl<switchboard>()}
    , clock_{pb->lookup_impl<relative_clock>()}
    , sensor_data_{load_data<pose_type>("state_groundtruth_estimate0", "pose_lookup", &read_data, switchboard_)}
    , sensor_data_it_{sensor_data_.cbegin()}
    , dataset_first_time_{sensor_data_it_->first}
    , vsync_estimate_{switchboard_->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")}
    /// TODO: Set with #198
    , enable_alignment_{switchboard_->get_env_bool("ILLIXR_ALIGNMENT_ENABLE", "False")}
    , init_pos_offset_{Eigen::Vector3f::Zero()}
    , align_rot_{Eigen::Matrix3f::Zero()}
    , align_trans_{Eigen::Vector3f::Zero()}
    , align_quat_{Eigen::Vector4f::Zero()}
    , align_scale_{0.0} {
    if (enable_alignment_) {
        std::string path_to_alignment(switchboard_->get_env("ILLIXR_ALIGNMENT_FILE", "./metrics/alignMatrix.txt"));
        load_align_parameters(path_to_alignment, align_rot_, align_trans_, align_quat_, align_scale_);
    }
    // Read position data of the first frame
    init_pos_offset_ = sensor_data_.cbegin()->second.position;

    auto newoffset = correct_pose(sensor_data_.begin()->second).orientation;
    set_offset(newoffset);
}

fast_pose_type pose_lookup_impl::get_fast_pose() const {
    const switchboard::ptr<const switchboard::event_wrapper<time_point>> estimated_vsync = vsync_estimate_.get_ro_nullable();
    if (estimated_vsync == nullptr) {
        spdlog::get("illixr")->trace("[pose_lookup] Vsync estimation not valid yet, returning fast_pose for now()");
        return get_fast_pose(clock_->now());
    } else {
        return get_fast_pose(**estimated_vsync);
    }
}

pose_type pose_lookup_impl::get_true_pose() const {
    throw std::logic_error{"Not Implemented"};
}

bool pose_lookup_impl::fast_pose_reliable() const {
    return true;
}

bool pose_lookup_impl::true_pose_reliable() const {
    return false;
}

Eigen::Quaternionf pose_lookup_impl::get_offset() {
    return offset_;
}

pose_type pose_lookup_impl::correct_pose(const pose_type& pose) const {
    pose_type swapped_pose;

    // Step 1: Compensate starting point to (0, 0, 0), pos only
    auto input_pose = pose_type{pose.sensor_time,
                                Eigen::Vector3f{
                                    pose.position(0) - init_pos_offset_(0),
                                    pose.position(1) - init_pos_offset_(1),
                                    pose.position(2) - init_pos_offset_(2),
                                },
                                pose.orientation};

    if (enable_alignment_) {
        // Step 2: Apply estimated alignment parameters
        // Step 2.1: Position alignment
        input_pose.position = align_scale_ * align_rot_ * input_pose.position + align_trans_;

        // Step 2.2: Orientation alignment
        Eigen::Vector4f quat_in    = {pose.orientation.x(), pose.orientation.y(), pose.orientation.z(), pose.orientation.w()};
        Eigen::Vector4f quat_out   = ori_multiply(quat_in, ori_inv(align_quat_));
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
    Eigen::Quaternionf raw_o(input_pose.orientation.w(), -input_pose.orientation.y(), input_pose.orientation.z(),
                             -input_pose.orientation.x());

    swapped_pose.orientation = apply_offset(raw_o);

    return swapped_pose;
}

void pose_lookup_impl::set_offset(const Eigen::Quaternionf& raw_o_times_offset) {
    std::unique_lock   lock{offset_mutex_};
    Eigen::Quaternionf raw_o = raw_o_times_offset * offset_.inverse();
    // std::cout << "pose_prediction: set_offset" << std::endl;
    offset_ = raw_o.inverse();
}

Eigen::Quaternionf pose_lookup_impl::apply_offset(const Eigen::Quaternionf& orientation) const {
    std::shared_lock lock{offset_mutex_};
    return orientation * offset_;
}

fast_pose_type pose_lookup_impl::get_fast_pose(time_point time) const {
    ullong lookup_time = time.time_since_epoch().count() + dataset_first_time_;

    auto nearest_row = sensor_data_.upper_bound(lookup_time);

    if (nearest_row == sensor_data_.cend()) {
#ifndef NDEBUG
        spdlog::get("illixr")->debug("[pose_lookup] Time {} ({} + {}) after last datum {}", lookup_time,
                                     std::chrono::nanoseconds(time.time_since_epoch()).count(), dataset_first_time_,
                                     sensor_data_.rbegin()->first);
#endif
        nearest_row--;
    } else if (nearest_row == sensor_data_.cbegin()) {
#ifndef NDEBUG
        spdlog::get("illixr")->debug("[pose_lookup] Time {} ({} + {}) before first datum {}", lookup_time,
                                     std::chrono::nanoseconds(time.time_since_epoch()).count(), dataset_first_time_,
                                     sensor_data_.cbegin()->first);
#endif
    } else {
        // "std::map::upper_bound" returns an iterator to the first pair whose key is GREATER than the argument.
        // I already know we aren't at the begin()
        // So I will decrement nearest_row here.
        nearest_row--;
    }

    auto looked_up_pose        = nearest_row->second;
    looked_up_pose.sensor_time = time_point{std::chrono::nanoseconds{nearest_row->first - dataset_first_time_}};
    return fast_pose_type{correct_pose(looked_up_pose), clock_->now(), time};
}

class pose_lookup_plugin : public plugin {
public:
    [[maybe_unused]] pose_lookup_plugin(const std::string& name, phonebook* pb)
        : plugin{name, pb} {
        pb->register_impl<pose_prediction>(std::static_pointer_cast<pose_prediction>(std::make_shared<pose_lookup_impl>(pb)));
    }
};

PLUGIN_MAIN(pose_lookup_plugin)
