#include "video_encoder.hpp"

#include "illixr/error_util.hpp"

#include <gst/app/gstappsrc.h>

using namespace ILLIXR;

void ada_video_encoder::enqueue(cv::Mat& img0, cv::Mat& img1) {
    const gsize sz0 = img0.total() * img0.elemSize();
    const gsize sz1 = img1.total() * img1.elemSize();

    // SAFER memory: copy into GstBuffer-owned memory (see §2 for zero-copy alt)
    GstBuffer* b0 = gst_buffer_new_allocate(nullptr, sz0, nullptr);
    GstBuffer* b1 = gst_buffer_new_allocate(nullptr, sz1, nullptr);

    GstMapInfo m0, m1;
    gst_buffer_map(b0, &m0, GST_MAP_WRITE);
    gst_buffer_map(b1, &m1, GST_MAP_WRITE);
    std::memcpy(m0.data, img0.data, sz0);
    std::memcpy(m1.data, img1.data, sz1);
    gst_buffer_unmap(b0, &m0);
    gst_buffer_unmap(b1, &m1);

    // ada_video_encoder.cpp (enqueue)
    const guint   fps_n = 30, fps_d = 1;
    const guint64 pts       = gst_util_uint64_scale(frame_idx_, (guint64) GST_SECOND * fps_d, fps_n);
    const guint64 dur       = gst_util_uint64_scale(1, (guint64) GST_SECOND * fps_d, fps_n);
    GST_BUFFER_PTS(b0)      = pts;
    GST_BUFFER_DURATION(b0) = dur;
    GST_BUFFER_PTS(b1)      = pts;
    GST_BUFFER_DURATION(b1) = dur;

    gst_app_src_push_buffer(GST_APP_SRC(appsrc_img0_), b0);
    gst_app_src_push_buffer(GST_APP_SRC(appsrc_img1_), b1);
    frame_idx_++;
}

GstFlowReturn ada_video_encoder::cb_appsink_msb(GstElement* sink) {
    std::lock_guard<std::mutex> lk(pipeline_sync_mutex_);

    if (samp0_) {
        if (auto* oldb = gst_sample_get_buffer(samp0_)) {
            gst_buffer_unmap(oldb, &img0_map_);
        }
        gst_sample_unref(samp0_);
        samp0_ = nullptr;
    }

    GstSample* sample = nullptr;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (!sample)
        return GST_FLOW_ERROR;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (!buffer || !gst_buffer_map(buffer, &img0_map_, GST_MAP_READ)) {
        if (sample)
            gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    samp0_      = sample;
    img0_ready_ = true;

    if (img1_ready_) {
        // both valid & mapped -> callback copies out synchronously
        callback_(img0_map_, img1_map_);
        // unmap and unref both
        gst_buffer_unmap(gst_sample_get_buffer(samp0_), &img0_map_);
        gst_buffer_unmap(gst_sample_get_buffer(samp1_), &img1_map_);
        gst_sample_unref(samp0_);
        samp0_ = nullptr;
        gst_sample_unref(samp1_);
        samp1_      = nullptr;
        img0_ready_ = img1_ready_ = false;
    }
    return GST_FLOW_OK;
}

GstFlowReturn ada_video_encoder::cb_appsink_lsb(GstElement* sink) {
    std::lock_guard<std::mutex> lk(pipeline_sync_mutex_);

    if (samp1_) {
        if (auto* oldb = gst_sample_get_buffer(samp1_)) {
            gst_buffer_unmap(oldb, &img1_map_);
        }
        gst_sample_unref(samp1_);
        samp1_ = nullptr;
    }

    GstSample* sample = nullptr;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (!sample)
        return GST_FLOW_ERROR;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (!buffer || !gst_buffer_map(buffer, &img1_map_, GST_MAP_READ)) {
        if (sample)
            gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    samp1_      = sample;
    img1_ready_ = true;

    if (img0_ready_) {
        callback_(img0_map_, img1_map_);
        gst_buffer_unmap(gst_sample_get_buffer(samp0_), &img0_map_);
        gst_buffer_unmap(gst_sample_get_buffer(samp1_), &img1_map_);
        gst_sample_unref(samp0_);
        samp0_ = nullptr;
        gst_sample_unref(samp1_);
        samp1_      = nullptr;
        img0_ready_ = img1_ready_ = false;
    }
    return GST_FLOW_OK;
} // namespace ILLIXR
