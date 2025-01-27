#include "plugin.hpp"

#include "illixr/relative_clock.hpp"

#include <opencv2/imgproc.hpp>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] webcam::webcam(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , _switchboard{pb_->lookup_impl<switchboard>()}
    , _frame_pub{_switchboard->get_writer<monocular_cam_type>("webcam")} {
    const char* video_stream = std::getenv("INPUT_VIDEO");
    _load_video              = video_stream != nullptr;
    if (_load_video) {
        _capture.open(video_stream);
    } else {
        _capture.open(0);
    }
    if (!_capture.isOpened()) {
        throw std::runtime_error("Cannot open camera");
    }
#if (CV_MAJOR_VERSION >= 3) && (CV_MINOR_VERSION >= 2)
    _capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    _capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    _capture.set(cv::CAP_PROP_FPS, 30);
#endif
}

void webcam::_p_one_iteration() {
    cv::Mat camera_frame_raw;
    _capture >> camera_frame_raw;
    if (camera_frame_raw.empty())
        return;
    cv::Mat camera_frame;
    cv::cvtColor(camera_frame_raw, camera_frame, cv::COLOR_BGR2RGB);
    time_point current_time(
        std::chrono::duration<long, std::nano>{std::chrono::system_clock::now().time_since_epoch().count()});
    _frame_pub.put(_frame_pub.allocate<monocular_cam_type>({current_time, camera_frame}));
}

PLUGIN_MAIN(webcam)