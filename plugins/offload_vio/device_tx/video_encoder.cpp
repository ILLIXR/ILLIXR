#include "video_encoder.hpp"

#include "illixr/error_util.hpp"

#include <chrono>
#include <gst/app/gstappsrc.h>
#include <thread>

namespace ILLIXR {



void vio_video_encoder::enqueue(cv::Mat& img0, cv::Mat& img1) {
    auto data_size      = img0.cols * img0.rows * img0.channels();
    int  size           = floor(data_size + ceil(img0.cols / 2.0) * ceil(img0.rows / 2.0) * 2);
    auto fill_zero_size = size - data_size;

    auto buffer_img0 = gst_buffer_new_and_alloc(size);
    auto buffer_img1 = gst_buffer_new_and_alloc(size);

    gst_buffer_fill(buffer_img0, 0, img0.data, data_size);
    gst_buffer_fill(buffer_img1, 0, img1.data, data_size);

    gst_buffer_memset(buffer_img0, data_size, 128, fill_zero_size);
    gst_buffer_memset(buffer_img1, data_size, 0, fill_zero_size);

    GST_BUFFER_OFFSET(buffer_img0) = num_samples_;
    GST_BUFFER_OFFSET(buffer_img1) = num_samples_;

    num_samples_++;

    auto ret_img0 = gst_app_src_push_buffer(reinterpret_cast<GstAppSrc*>(appsrc_img0_), buffer_img0);
    auto ret_img1 = gst_app_src_push_buffer(reinterpret_cast<GstAppSrc*>(appsrc_img1_), buffer_img1);
    if (ret_img0 != GST_FLOW_OK || ret_img1 != GST_FLOW_OK) {
        abort("Failed to push buffer");
    }
}

GstFlowReturn vio_video_encoder::cb_appsink(GstElement* sink) {
    GstSample* sample;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);

        std::unique_lock<std::mutex> lock(pipeline_sync_mutex_); // lock acquired
        if (sink == appsink_img0_) {
            gst_buffer_map(buffer, &img0_map_, GST_MAP_READ);
            img0_ready_ = true;
        } else {
            gst_buffer_map(buffer, &img1_map_, GST_MAP_READ);
            img1_ready_ = true;
        }

        if (img0_ready_ && img1_ready_) {
            callback_(img0_map_, img1_map_);
            img0_ready_ = false;
            img1_ready_ = false;
            lock.unlock(); // unlock and notify the waiting thread to clean up
            pipeline_sync_.notify_one();
        } else {
            pipeline_sync_.wait(lock, [&]() {
                return !img0_ready_ && !img1_ready_;
            });            // wait unlocks the mutex if condition is not met
            lock.unlock(); // wait has acquired the lock. unlock it and start cleaning up
        }

        if (sink == appsink_img0_) {
            gst_buffer_unmap(buffer, &img0_map_);
        } else {
            gst_buffer_unmap(buffer, &img1_map_);
        }
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    return GST_FLOW_ERROR;
}

} // namespace ILLIXR
