#include "video_decoder.hpp"

#include <cstring>
#include <gst/app/gstappsrc.h>

using namespace ILLIXR;

void ada_video_decoder::enqueue(std::string& img0, std::string& img1) {
    size_t     img0_size   = img0.size();
    size_t     img1_size   = img1.size();
    GstBuffer* buffer_img0 = gst_buffer_new_allocate(nullptr, img0_size, nullptr);
    GstBuffer* buffer_img1 = gst_buffer_new_allocate(nullptr, img1_size, nullptr);
    gst_buffer_fill(buffer_img0, 0, img0.data(), img0_size);
    gst_buffer_fill(buffer_img1, 0, img1.data(), img1_size);

    auto ret_img0 = gst_app_src_push_buffer(GST_APP_SRC(appsrc_img0_), buffer_img0);
    auto ret_img1 = gst_app_src_push_buffer(GST_APP_SRC(appsrc_img1_), buffer_img1);
    if (ret_img0 != GST_FLOW_OK || ret_img1 != GST_FLOW_OK) {
        abort("Failed to push buffer");
    }
}
