#include "illixr/video_decoder.hpp"

using namespace ILLIXR;

void video_decoder::create_pipelines() {
    gst_init(nullptr, nullptr);

    appsrc_img0_  = gst_element_factory_make("appsrc", "appsrc_img0");
    appsrc_img1_  = gst_element_factory_make("appsrc", "appsrc_img1");
    appsink_img0_ = gst_element_factory_make("appsink", "appsink_img0");
    appsink_img1_ = gst_element_factory_make("appsink", "appsink_img1");

    auto nvvideoconvert_0 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert0");
    auto nvvideoconvert_1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1");

    auto decoder_img0 = gst_element_factory_make("nvv4l2decoder", "decoder_img0");
    auto decoder_img1 = gst_element_factory_make("nvv4l2decoder", "decoder_img1");

    auto      caps_filter_0 = gst_element_factory_make("capsfilter", "caps_filter_0");
    auto      caps_filter_1 = gst_element_factory_make("capsfilter", "caps_filter_1");
    _GstCaps* caps_x26x;
#if defined ADA
    auto caps_raw_gray8 = gst_caps_from_string("video/x-raw,format=GRAY8");
    g_object_set(G_OBJECT(caps_filter_0), "caps", caps_raw_gray8, nullptr);
    g_object_set(G_OBJECT(caps_filter_1), "caps", caps_raw_gray8, nullptr);
    gst_caps_unref(caps_raw_gray8);

    // create caps with width and height
    caps_x26x = gst_caps_new_simple("video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING,
                                    "au", nullptr);
#elif defined VIO
    g_object_set(G_OBJECT(caps_filter_0), "caps", gst_caps_from_string("video/x-raw,format=GRAY8"), nullptr);
    g_object_set(G_OBJECT(caps_filter_1), "caps", gst_caps_from_string("video/x-raw,format=GRAY8"), nullptr);
    // create caps with width and height
    caps_x26x = gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING,
                                    "au", nullptr);
#endif
    g_object_set(G_OBJECT(appsrc_img0_), "caps", caps_x26x, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "caps", caps_x26x, nullptr);
    gst_caps_unref(caps_x26x);
#if defined ADA
    g_object_set(appsrc_img0_, "is-live", TRUE, "format", GST_FORMAT_TIME, "do-timestamp", TRUE, nullptr);
    g_object_set(appsrc_img1_, "is-live", TRUE, "format", GST_FORMAT_TIME, "do-timestamp", TRUE, nullptr);
#elif defined VIO
    g_object_set(G_OBJECT(appsrc_img0_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);
#endif
    // works on server but not usable if you are offloading to jetson
    g_object_set(G_OBJECT(decoder_img0), "low-latency-mode", TRUE, nullptr);
    g_object_set(G_OBJECT(decoder_img1), "low-latency-mode", TRUE, nullptr);

    g_object_set(appsink_img0_, "emit-signals", TRUE, "sync", FALSE, nullptr);
    g_object_set(appsink_img1_, "emit-signals", TRUE, "sync", FALSE, nullptr);

    g_signal_connect(appsink_img0_, "new-sample", G_CALLBACK(cb_new_sample), this);
    g_signal_connect(appsink_img1_, "new-sample", G_CALLBACK(cb_new_sample), this);

    pipeline_img0_ = gst_pipeline_new("pipeline_img0");
    pipeline_img1_ = gst_pipeline_new("pipeline_img1");

    gst_bin_add_many(GST_BIN(pipeline_img0_), appsrc_img0_, decoder_img0, nvvideoconvert_0, caps_filter_0, appsink_img0_,
                     nullptr);
    gst_bin_add_many(GST_BIN(pipeline_img1_), appsrc_img1_, decoder_img1, nvvideoconvert_1, caps_filter_1, appsink_img1_,
                     nullptr);

    // link elements
    if (!gst_element_link_many(appsrc_img0_, decoder_img0, nvvideoconvert_0, caps_filter_0, appsink_img0_, nullptr) ||
        !gst_element_link_many(appsrc_img1_, decoder_img1, nvvideoconvert_1, caps_filter_1, appsink_img1_, nullptr)) {
        abort("Failed to link elements");
    }

    gst_element_set_state(pipeline_img0_, GST_STATE_PLAYING);
    gst_element_set_state(pipeline_img1_, GST_STATE_PLAYING);
}

GstFlowReturn video_decoder::cb_appsink(GstElement* sink) {
    GstSample* sample;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);

        std::unique_lock<std::mutex> lock(pipeline_sync_mutex_); // lock acquired
#ifdef ADA
        GstMapInfo* map = nullptr;
#endif
        if (sink == appsink_img0_) {
            if (!gst_buffer_map(buffer, &img0_map_, GST_MAP_READ)) {
                gst_sample_unref(sample);
                return GST_FLOW_ERROR;
            }
            img0_ready_ = true;
#ifdef ADA
            std::memcpy(msb_owned_.data, img0_map_.data, 640 * 480);
            map = &img0_map_;
#endif
        } else {
            if (!gst_buffer_map(buffer, &img1_map_, GST_MAP_READ)) {
                gst_sample_unref(sample);
                return GST_FLOW_ERROR;
            }
            img1_ready_ = true;
#ifdef ADA
            std::memcpy(lsb_owned_.data, img1_map_.data, 640 * 480);
            map = &img1_map_;
#endif
        }
        if (img0_ready_ && img1_ready_) {
#if defined ADA
            cv::Mat msb(msb_owned_);
            cv::Mat lsb(lsb_owned_);
            callback_(std::move(msb), std::move(lsb));
#elif defined VIO
            callback_(cv::Mat(IMG_HEIGHT, IMG_WIDTH, CV_8UC1, img0_map_.data),
                      cv::Mat(IMG_HEIGHT, IMG_WIDTH, CV_8UC1, img1_map_.data));
#endif
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
#if defined ADA
        gst_buffer_unmap(buffer, map);
#elif defined VIO
        if (sink == appsink_img0_) {
            gst_buffer_unmap(buffer, &img0_map_);
        } else {
            gst_buffer_unmap(buffer, &img1_map_);
        }
#endif
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    return GST_FLOW_ERROR;
}
