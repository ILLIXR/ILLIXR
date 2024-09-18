#include "plugin.hpp"

#include "illixr/relative_clock.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
using namespace ILLIXR;

[[maybe_unused]] webcam::webcam(const std::string& name_, phonebook* pb_) : threadloop{name_, pb_}
    , _switchboard{pb_->lookup_impl<switchboard>()}
    , _frame_pub{_switchboard->get_writer<monocular_cam_type>("webcam")} {
    char* fp = getenv("OCV_FPS");
    if (fp) {
        char* ep;
        double t = strtod(fp, &ep);
        if (fp != ep)
            fps = t;
    }
    std::cout << "FPS " << fps << std::endl;
    const char* video_stream = std::getenv("INPUT_VIDEO");
    _load_video = video_stream != nullptr;
    if (_load_video) {
        _capture.open(video_stream);
    } else {
        _capture.open(0);
    }
    if (!_capture.isOpened()){
        throw std::runtime_error("Cannot open camera");
    }
    cv::Mat temp = cv::imread("/home/friedel/devel/ILLIXR/cmake-build-native/file_2.png", cv::IMREAD_COLOR);
    if (temp.empty())
        std::cout << "EMPTY IMAGE FILE" << std::endl;
    cv::cvtColor(temp, last_image, cv::COLOR_BGR2RGB);
    last_send = 0;
#if (CV_MAJOR_VERSION >= 3) && (CV_MINOR_VERSION >= 2)
    _capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    _capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    _capture.set(cv::CAP_PROP_FPS, fps);
#endif

}

void webcam::_p_one_iteration() {
    size_t now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    if ((now - last_send) >= (1e9 / fps)) {
        std::cout << "  Image " << last_send << std::endl;
        time_point current_time(std::chrono::duration<long, std::nano>{std::chrono::system_clock::now().time_since_epoch().count()});
        _frame_pub.put(_frame_pub.allocate<monocular_cam_type>({current_time, last_image}));
        last_send = now;
    }
}


PLUGIN_MAIN(webcam)