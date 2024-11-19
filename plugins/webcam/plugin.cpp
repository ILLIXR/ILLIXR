#include "plugin.hpp"

#include "illixr/relative_clock.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include "include/data_format.hpp"

using namespace ILLIXR;

void get_images(const uint64_t tp, const new_map& left_data, const new_map& right_data,
                cv::Mat& left_img, cv::Mat& right_img) {
    left_img = cv::imread(files::camL_path.string() + "/" + left_data.at(tp), cv::IMREAD_UNCHANGED);
    right_img = cv::imread(files::camR_path.string() + "/" + right_data.at(tp), cv::IMREAD_UNCHANGED);
}

[[maybe_unused]] webcam::webcam(const std::string& name_, phonebook* pb_) : threadloop{name_, pb_}
    , _switchboard{pb_->lookup_impl<switchboard>()}
    , _frame_pub{_switchboard->get_writer<binocular_cam_type>("webcam")} {
    std::string sub_path = "fps30_dur3_CCF" ;
    files* fls = files::getInstance(sub_path);
    files::load_left_data(left_data);
    files::load_right_data(right_data);
}

void webcam::_p_one_iteration() {
    cv::Mat left_img, right_img;
    if (written)
        return;
    for (const auto& item : left_data) {
        std::cout << "Publishing " << item.first << std::endl;
        get_images(item.first, left_data, right_data, left_img, right_img);
        auto tp = time_point(_clock_duration(std::chrono::nanoseconds(item.first)));
        std::cout << " tp " << tp.time_since_epoch().count() << std::endl;
        _frame_pub.put(_frame_pub.allocate<binocular_cam_type>({tp, left_img, right_img}));
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    written = true;
}

PLUGIN_MAIN(webcam)