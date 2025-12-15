#pragma once

#include "illixr/data_format/opencv_data_types.hpp"
#include "illixr/data_loading.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/threadloop.hpp"

#include <opencv2/imgcodecs.hpp>

namespace ILLIXR {

/*
 * Uncommenting this preprocessor macro makes the offline_cam load each data from the disk as it is needed.
 * Otherwise, we load all of them at the beginning, hold them in memory, and drop them in the queue as needed.
 * Lazy loading has an artificial negative impact on performance which is absent from an online-sensor system.
 * Eager loading deteriorates the startup time and uses more memory.
 */
// #define LAZY

#ifdef ILLIXR_ANDROID_BUILD
#define LLI lazy_load_image*
#else
#define LLI lazy_load_image
#endif

class lazy_load_image {
public:
    lazy_load_image() = default;

    explicit lazy_load_image(std::string path)
        : path_(std::move(path)) {
#ifndef LAZY
        mat_ = cv::imread(path_, cv::IMREAD_GRAYSCALE);
#endif
    }

    [[nodiscard]] cv::Mat load() const {
#ifdef LAZY
        cv::Mat mat_ = cv::imread(path_, cv::IMREAD_GRAYSCALE);
    #error "Linux scheduler cannot interrupt IO work, so lazy-loading is unadvisable."
#endif
        assert(!mat_.empty());
        return mat_;
    }

private:
    std::string path_{};
    cv::Mat     mat_{};
};

typedef struct {
#ifdef ILLIXR_ANDROID_BUILD
    LLI cam0 = nullptr;
    LLI cam1 = nullptr;
#else
    LLI cam0;
    LLI cam1;
#endif

#ifdef ILLIXR_ANDROID_BUILD
    ~sensor_types() {
        delete cam0;
        delete cam1;
}
#endif
} sensor_types;

class offline_cam : public threadloop {
public:
    [[maybe_unused]] offline_cam(const std::string& name, phonebook* pb);
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

private:
    const std::shared_ptr<switchboard>                   switchboard_;
    switchboard::writer<data_format::binocular_cam_type> cam_publisher_;
    const std::map<ullong, sensor_types>                 sensor_data_;
    ullong                                               dataset_first_time_;
    ullong                                               last_timestamp_;
    std::shared_ptr<relative_clock>                      clock_;
    std::map<ullong, sensor_types>::const_iterator       next_row_;
};
} // namespace ILLIXR
