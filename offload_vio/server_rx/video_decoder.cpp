//
// Created by steven on 7/8/22.
//

#include "video_decoder.h"

#include <utility>
#include "common/error_util.hpp"
#include <gst/app/gstappsrc.h>

namespace ILLIXR {

    GstFlowReturn cb_new_sample(GstElement *appsink, gpointer* user_data) {
        return reinterpret_cast<video_decoder *>(user_data)->cb_appsink(appsink);
    }

    void video_decoder::create_pipelines() {
        gst_init(nullptr, nullptr);

        _appsrc_img0 = gst_element_factory_make("appsrc", "appsrc_img0");
        _appsrc_img1 = gst_element_factory_make("appsrc", "appsrc_img1");
        _appsink_img0 = gst_element_factory_make("appsink", "appsink_img0");
        _appsink_img1 = gst_element_factory_make("appsink", "appsink_img1");

        auto nvvideoconvert_0 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert0");
        auto nvvideoconvert_1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1");

        auto videoconvert_0 = gst_element_factory_make("videoconvert", "videoconvert0");
        auto videoconvert_1 = gst_element_factory_make("videoconvert", "videoconvert1");

        auto h265parse_0 = gst_element_factory_make("h265parse", "h265parse0");
        auto h265parse_1 = gst_element_factory_make("h265parse", "h265parse1");

        auto decoder_img0 = gst_element_factory_make("nvv4l2decoder", "decoder_img0");
        auto decoder_img1 = gst_element_factory_make("nvv4l2decoder", "decoder_img1");

        auto caps_filter_0 = gst_element_factory_make("capsfilter", "caps_filter_0");
        auto caps_filter_1 = gst_element_factory_make("capsfilter", "caps_filter_1");

        g_object_set(G_OBJECT(caps_filter_0), "caps", gst_caps_from_string("video/x-raw,format=GRAY8"), nullptr);
        g_object_set(G_OBJECT(caps_filter_1), "caps", gst_caps_from_string("video/x-raw,format=GRAY8"), nullptr);

        // create caps with width and height
        auto caps_x265 = gst_caps_new_simple("video/x-h264",
                                             "stream-format", G_TYPE_STRING, "byte-stream",
                                             "alignment", G_TYPE_STRING, "au",
                                             nullptr);
        g_object_set(G_OBJECT(_appsrc_img0), "caps", caps_x265, nullptr);
        g_object_set(G_OBJECT(_appsrc_img1), "caps", caps_x265, nullptr);
        gst_caps_unref(caps_x265);

        g_object_set (G_OBJECT (_appsrc_img0),
                      "stream-type", 0,
                      "format", GST_FORMAT_BYTES,
                      "is-live", TRUE,
                      nullptr);
        g_object_set (G_OBJECT (_appsrc_img1),
                      "stream-type", 0,
                      "format", GST_FORMAT_BYTES,
                      "is-live", TRUE,
                      nullptr);

        g_object_set (G_OBJECT(decoder_img0), "low-latency-mode", TRUE, nullptr);
        g_object_set (G_OBJECT(decoder_img1), "low-latency-mode", TRUE, nullptr);

        g_object_set(_appsink_img0, "emit-signals", TRUE, "sync", FALSE, nullptr);
        g_object_set(_appsink_img1, "emit-signals", TRUE, "sync", FALSE, nullptr);

        g_signal_connect(_appsink_img0, "new-sample", G_CALLBACK(cb_new_sample), this);
        g_signal_connect(_appsink_img1, "new-sample", G_CALLBACK(cb_new_sample), this);

        _pipeline_img0 = gst_pipeline_new("pipeline_img0");
        _pipeline_img1 = gst_pipeline_new("pipeline_img1");

        auto identity = gst_element_factory_make("identity", "identity");
        g_object_set(G_OBJECT(identity), "dump", TRUE, nullptr);
        g_object_set(G_OBJECT(identity), "signal-handoffs", TRUE, nullptr);

        gst_bin_add_many(GST_BIN(_pipeline_img0), _appsrc_img0, h265parse_0, identity, decoder_img0, videoconvert_0, nvvideoconvert_0, caps_filter_0, _appsink_img0, nullptr);
        gst_bin_add_many(GST_BIN(_pipeline_img1), _appsrc_img1, h265parse_1, decoder_img1, videoconvert_1, nvvideoconvert_1, caps_filter_1, _appsink_img1, nullptr);

        // link elements
        if (!gst_element_link_many(_appsrc_img0, decoder_img0, nvvideoconvert_0, videoconvert_0, caps_filter_0, _appsink_img0, nullptr) ||
            !gst_element_link_many(_appsrc_img1, decoder_img1, nvvideoconvert_1, videoconvert_1, caps_filter_1, _appsink_img1, nullptr)) {
            abort("Failed to link elements");
        }

        gst_element_set_state(_pipeline_img0, GST_STATE_PLAYING);
        gst_element_set_state(_pipeline_img1, GST_STATE_PLAYING);
    }

    video_decoder::video_decoder(std::function<void(cv::Mat &&, cv::Mat &&)> callback): _callback(std::move(callback)) {

    }

    void video_decoder::init() {
        create_pipelines();
    }

    void video_decoder::enqueue(std::string& img0, std::string& img1) {
        auto buffer_img0 = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, img0.data(),
                                                       img0.size(), 0,
                                                       img0.size(), nullptr, nullptr);
        auto buffer_img1 = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, img1.data(),
                                                       img1.size(), 0,
                                                       img1.size(), nullptr, nullptr);

        GST_BUFFER_OFFSET(buffer_img0) = _num_samples;
        GST_BUFFER_OFFSET(buffer_img1) = _num_samples;

        _num_samples++;

        auto ret_img0 = gst_app_src_push_buffer(reinterpret_cast<GstAppSrc *>(_appsrc_img0), buffer_img0);
        auto ret_img1 = gst_app_src_push_buffer(reinterpret_cast<GstAppSrc *>(_appsrc_img1), buffer_img1);
        
        if (
                ret_img0 != GST_FLOW_OK ||
                ret_img1 != GST_FLOW_OK) {
            abort("Failed to push buffer");
        }
    }

    GstFlowReturn video_decoder::cb_appsink(GstElement *sink) {
        // std::cout << "cb_appsink" << std::endl;

        GstSample *sample;
        g_signal_emit_by_name(sink, "pull-sample", &sample);
        if (sample) {
            GstBuffer *buffer = gst_sample_get_buffer(sample);

            std::unique_lock<std::mutex> lock(_pipeline_sync_mutex); // lock acquired
            if (sink == _appsink_img0) {
                gst_buffer_map(buffer, &_img0_map, GST_MAP_READ);
                _img0_ready = true;
            } else {
                gst_buffer_map(buffer, &_img1_map, GST_MAP_READ);
                _img1_ready = true;
            }

            if (_img0_ready && _img1_ready) {
                _callback(cv::Mat(480, 752, CV_8UC1, _img0_map.data),
                          cv::Mat(480, 752,  CV_8UC1, _img1_map.data));
                // _callback(cv::Mat(376, 672, CV_8UC1, _img0_map.data),
                //           cv::Mat(376, 672,  CV_8UC1, _img1_map.data));
                _img0_ready = false;
                _img1_ready = false;
                lock.unlock(); // unlock and notify the waiting thread to clean up
                _pipeline_sync.notify_one();
            } else {
                _pipeline_sync.wait(lock, [&]() {
                    return !_img0_ready && !_img1_ready;
                }); // wait unlocks the mutex if condition is not met
                lock.unlock(); // wait has acquired the lock. unlock it and start cleaning up
            }

            if (sink == _appsink_img0) {
                gst_buffer_unmap(buffer, &_img0_map);
            } else {
                gst_buffer_unmap(buffer, &_img1_map);
            }
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }
        return GST_FLOW_ERROR;
    }

} // ILLIXR