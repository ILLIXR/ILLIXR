#pragma once

#include "illixr/data_format/misc.hpp"
#include "illixr/data_format/pose.hpp"
#include "illixr/data_format/scene_reconstruction.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <map>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <utility>

namespace ILLIXR {

class lazy_load_image {
public:
    // pyh: load image immediately; keep it in memory for future copies
    explicit lazy_load_image(std::string path)
        : path_(std::move(path))
        , image_(std::make_unique<cv::Mat>(cv::imread(path_, cv::IMREAD_UNCHANGED))) {
        assert(!image_->empty());
    }

    // pyh: default constructor
    lazy_load_image() = default;

    // pyh: return a heap-allocated COPY of the stored image
    [[nodiscard]] std::unique_ptr<cv::Mat> load() const {
        // pyh: if default-constructed and never loaded, return an empty mat copy
        if (!image_ || image_->empty()) {
            return std::make_unique<cv::Mat>();
        }
        return std::make_unique<cv::Mat>(*image_);
    }

    // pyh: return a COPY converted to RGBA (4 channels).
    [[maybe_unused]] [[nodiscard]] std::unique_ptr<cv::Mat> color_load() const {
        auto out = std::make_unique<cv::Mat>(image_->size(), CV_8UC4);
        cv::cvtColor(*image_, *out, cv::COLOR_BGR2RGBA, 0);
        return out;
    }

private:
    std::string              path_;
    std::unique_ptr<cv::Mat> image_; // Added member variable to store the loaded image
};

struct sensor_types {
    // pyh since we are using groundtruth pose change the datatype
    data_format::pose_type pose;
    lazy_load_image        depth_cam;
    lazy_load_image        color_cam;
    [[maybe_unused]] bool  last_frame = false;
};

class offline_scannet : public ILLIXR::threadloop {
public:
    [[maybe_unused]] offline_scannet(const std::string& name_, phonebook* pb_);

protected:
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

private:
    const std::map<ullong, sensor_types>               sensor_data_;
    std::map<ullong, sensor_types>::const_iterator     sensor_data_it_;
    const std::shared_ptr<switchboard>                 switchboard_;
    std::shared_ptr<const relative_clock>              clock_;
    switchboard::writer<data_format::scene_recon_type> scannet_;

    unsigned current_frame_count_ = 0;
    unsigned frame_count_;
};

} // namespace ILLIXR
