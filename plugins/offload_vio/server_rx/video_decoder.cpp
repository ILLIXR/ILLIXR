#include "video_decoder.hpp"

#include "illixr/error_util.hpp"

#include <gst/app/gstappsrc.h>

using namespace ILLIXR;

void vio_video_decoder::enqueue(std::string& img0, std::string& img1) {
    auto buffer_img0 =
        gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, img0.data(), img0.size(), 0, img0.size(), nullptr, nullptr);
    auto buffer_img1 =
        gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, img1.data(), img1.size(), 0, img1.size(), nullptr, nullptr);

    GST_BUFFER_OFFSET(buffer_img0) = num_samples_;
    GST_BUFFER_OFFSET(buffer_img1) = num_samples_;

    num_samples_++;

    auto ret_img0 = gst_app_src_push_buffer(reinterpret_cast<GstAppSrc*>(appsrc_img0_), buffer_img0);
    auto ret_img1 = gst_app_src_push_buffer(reinterpret_cast<GstAppSrc*>(appsrc_img1_), buffer_img1);

    if (ret_img0 != GST_FLOW_OK || ret_img1 != GST_FLOW_OK) {
        abort("Failed to push buffer");
    }
}
