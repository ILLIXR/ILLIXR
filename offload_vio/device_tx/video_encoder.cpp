//
// Created by steven on 7/3/22.
//

#include "video_encoder.h"
#include "common/error_util.hpp"
#include <gst/app/gstappsrc.h>
#include <chrono>
#include <thread>
#include <stdlib.h>


namespace ILLIXR {
//
//    void cb_need_data(GstElement *appsrc, guint size, gpointer* user_data) {
//        reinterpret_cast<video_encoder*>(user_data)->want = 1;
//    }

    GstFlowReturn cb_new_sample(GstElement *appsink, gpointer* user_data) {
        return reinterpret_cast<video_encoder *>(user_data)->cb_appsink(appsink);
    }

    video_encoder::video_encoder(std::function<void(const GstMapInfo&, const GstMapInfo&)> callback) : _callback(std::move(callback)) {

    }

    void video_encoder::create_pipelines() {
        gst_init(nullptr, nullptr);

        _appsrc_img0 = gst_element_factory_make("appsrc", "appsrc_img0");
        _appsrc_img1 = gst_element_factory_make("appsrc", "appsrc_img1");
        _appsink_img0 = gst_element_factory_make("appsink", "appsink_img0");
        _appsink_img1 = gst_element_factory_make("appsink", "appsink_img1");

        auto videoconvert_0 = gst_element_factory_make("videoconvert", "videoconvert0");
        auto videoconvert_1 = gst_element_factory_make("videoconvert", "videoconvert1");

        auto caps_filter_0 = gst_element_factory_make("capsfilter", "caps_filter0");
        auto caps_filter_1 = gst_element_factory_make("capsfilter", "caps_filter1");

        auto nvvideoconvert_0 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert0");
        auto nvvideoconvert_1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1");

        auto encoder_img0 = gst_element_factory_make("nvv4l2h264enc", "encoder_img0");
        auto encoder_img1 = gst_element_factory_make("nvv4l2h264enc", "encoder_img1");

        auto h265parse_0 = gst_element_factory_make("h265parse", "h265parse0");
        auto h265parse_1 = gst_element_factory_make("h265parse", "h265parse1");

        auto caps_8uc1 = gst_caps_from_string("video/x-raw,format=GRAY8,width=672,height=376,framerate=0/1"); // 752/480 for euroc
        g_object_set(G_OBJECT(_appsrc_img0), "caps", caps_8uc1, nullptr);
        g_object_set(G_OBJECT(_appsrc_img1), "caps", caps_8uc1, nullptr);
        gst_caps_unref(caps_8uc1);

        auto caps_convert_to = gst_caps_from_string("video/x-raw,format=NV12,width=672,height=376"); // 752/480 for euroc
        g_object_set(G_OBJECT(caps_filter_0), "caps", caps_convert_to, nullptr);
        g_object_set(G_OBJECT(caps_filter_1), "caps", caps_convert_to, nullptr);
        gst_caps_unref(caps_convert_to);

        // set bitrate
        // g_object_set(G_OBJECT(encoder_img0), "bitrate", std::stoi(std::getenv("ILLIXR_BITRATE")), nullptr, 10);
        // g_object_set(G_OBJECT(encoder_img1), "bitrate", std::stoi(std::getenv("ILLIXR_BITRATE")), nullptr, 10);
        // 50Mbps = 52428800
        // 20Mbps = 20971520
        // 10Mbps = 10485760
        // 5Mbps = 5242880
        // 2Mbps = 2097152
        // 0.5Mbps = 524288
        // 0.1Mbps = 104857
        g_object_set(G_OBJECT(encoder_img0), "bitrate", 5242880, nullptr);
        g_object_set(G_OBJECT(encoder_img1), "bitrate", 5242880, nullptr);

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

        g_object_set(_appsink_img0, "emit-signals", TRUE, "sync", FALSE, nullptr);
        g_object_set(_appsink_img1, "emit-signals", TRUE, "sync", FALSE, nullptr);

        g_signal_connect(_appsink_img0, "new-sample", G_CALLBACK(cb_new_sample), this);
        g_signal_connect(_appsink_img1, "new-sample", G_CALLBACK(cb_new_sample), this);

        _pipeline_img0 = gst_pipeline_new("pipeline_img0");
        _pipeline_img1 = gst_pipeline_new("pipeline_img1");

        gst_bin_add_many(GST_BIN(_pipeline_img0), _appsrc_img0, nvvideoconvert_0, encoder_img0, h265parse_0, caps_filter_0, videoconvert_0, _appsink_img0, nullptr);
        gst_bin_add_many(GST_BIN(_pipeline_img1), _appsrc_img1, nvvideoconvert_1, encoder_img1, h265parse_1, caps_filter_1, videoconvert_1, _appsink_img1, nullptr);

        // link elements
        if (!gst_element_link_many(_appsrc_img0, nvvideoconvert_0, encoder_img0, _appsink_img0, nullptr) ||
            !gst_element_link_many(_appsrc_img1, nvvideoconvert_1, encoder_img1, _appsink_img1, nullptr)) {
            abort("Failed to link elements");
        }

        gst_element_set_state(_pipeline_img0, GST_STATE_PLAYING);
        gst_element_set_state(_pipeline_img1, GST_STATE_PLAYING);
    }

    void video_encoder::enqueue(cv::Mat& img0, cv::Mat& img1) {
        // push cv mat into appsrc
        // print img0 size
        auto data_size = img0.cols * img0.rows * img0.channels();
        int size = floor(data_size + ceil(img0.cols / 2.0) * ceil(img0.rows / 2.0) * 2);
        auto fill_zero_size = size - data_size;

        auto buffer_img0 = gst_buffer_new_and_alloc(size);
        auto buffer_img1 = gst_buffer_new_and_alloc(size);

        gst_buffer_fill (buffer_img0, 0, img0.data, data_size);
        gst_buffer_fill (buffer_img1, 0, img1.data, data_size);

        gst_buffer_memset(buffer_img0, data_size, 128, fill_zero_size);
        gst_buffer_memset(buffer_img1, data_size, 0, fill_zero_size);
        // auto buffer_img0 = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, img0.data,
        //                                                data_size, 0,
        //                                                data_size, nullptr, nullptr);
        // auto buffer_img1 = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, img1.data,
        //                                                data_size, 0,
        //                                                data_size, nullptr, nullptr);

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

    void video_encoder::init() {
        create_pipelines();
    }

    GstFlowReturn video_encoder::cb_appsink(GstElement *sink) {
        // print thread id
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
                _callback(_img0_map, _img1_map);
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