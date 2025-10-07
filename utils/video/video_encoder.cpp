#include "video_encoder.hpp"

using namespace ILLIXR;

void video_encoder::create_pipelines() {
    gst_init(nullptr, nullptr);

    // ADA: 0 MSB image encoding pipeline
    // ADA: 1 LSB image encoding pipeline
    appsrc_img0_  = gst_element_factory_make("appsrc", "appsrc_img0");
    appsrc_img1_  = gst_element_factory_make("appsrc", "appsrc_img1");
    appsink_img0_ = gst_element_factory_make("appsink", "appsink_img0");
    appsink_img1_ = gst_element_factory_make("appsink", "appsink_img1");

    auto encoder_img0 = gst_element_factory_make(ILLIXR_ENCODING, "encoder_img0");
    auto encoder_img1 = gst_element_factory_make(ILLIXR_ENCODING, "encoder_img1");

    auto caps_8uc1 =
        gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "GRAY8", "framerate", GST_TYPE_FRACTION, TYPE_FRACTION, 1,
                            "width", G_TYPE_INT, IMG_WIDTH, "height", G_TYPE_INT, IMG_HEIGHT, NULL);

    g_object_set(G_OBJECT(appsrc_img0_), "caps", caps_8uc1, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "caps", caps_8uc1, nullptr);
    gst_caps_unref(caps_8uc1);

    // set bitrate from environment variables
    // g_object_set(G_OBJECT(encoder_img0), "bitrate", std::stoi(sb->get_env("ILLIXR_BITRATE")), nullptr, 10);
    // g_object_set(G_OBJECT(encoder_img1), "bitrate", std::stoi(sb->get_env("ILLIXR_BITRATE")), nullptr, 10);

    // set bitrate from defined variables
    g_object_set(G_OBJECT(encoder_img1), "bitrate", ILLIXR_BITRATE, nullptr);
#if defined VIO
    auto nvvideoconvert_0 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert0");
    auto nvvideoconvert_1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1");
    g_object_set(G_OBJECT(encoder_img0), "bitrate", ILLIXR_BITRATE, nullptr);
    g_object_set(G_OBJECT(appsrc_img0_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);
#elif defined ADA

    // CPU colorspace (system memory) → I420
    auto vc_sys0 = gst_element_factory_make("videoconvert", "videoconvert_sys0");

    auto nvvideoconvert_0  = gst_element_factory_make("nvvidconv", "nvvideoconvert0");
    auto nvvideoconvert_1  = gst_element_factory_make("nvvidconv", "nvvideoconvert1");
    auto nvvideoconvert_0a = gst_element_factory_make("nvvidconv", "nvvideoconvert0a");
    auto parse0            = gst_element_factory_make("h265parse", "h265parse0");
    auto parse1            = gst_element_factory_make("h265parse", "h265parse1");

    auto cf_i420_sys0     = gst_element_factory_make("capsfilter", "cf_i420_sys0");
    auto cap_cf_i420_sys0 = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", nullptr);

    g_object_set(G_OBJECT(cf_i420_sys0), "caps", cap_cf_i420_sys0, nullptr);
    gst_caps_unref(cap_cf_i420_sys0);

    auto     cf_i420_nvmm0 = gst_element_factory_make("capsfilter", "cf_i420_nvmm0");
    GstCaps* cap_i420      = gst_caps_from_string("video/x-raw(memory:NVMM), format=I420");
    g_object_set(G_OBJECT(cf_i420_nvmm0), "caps", cap_i420, nullptr);
    gst_caps_unref(cap_i420);

    auto     cf_nv24_nvmm0 = gst_element_factory_make("capsfilter", "cf_nv24_nvmm0");
    GstCaps* cap_nv24      = gst_caps_from_string("video/x-raw(memory:NVMM), format=NV24");
    g_object_set(G_OBJECT(cf_nv24_nvmm0), "caps", cap_nv24, nullptr);
    gst_caps_unref(cap_nv24);

    // flag for orin
    g_object_set(G_OBJECT(encoder_img0), "enable-lossless", TRUE, nullptr);

    // pyh: Set to a large value to avoid periodic latency spikes from keyframe insertion.
    // should be safe for our use case because we don't need random access to old frames.
    g_object_set(G_OBJECT(encoder_img0), "idrinterval", 25600, nullptr);
    g_object_set(G_OBJECT(encoder_img1), "idrinterval", 25600, nullptr);

    g_object_set(G_OBJECT(encoder_img0), "iframeinterval", 25600, nullptr);
    g_object_set(G_OBJECT(encoder_img1), "iframeinterval", 25600, nullptr);

    g_object_set(G_OBJECT(appsrc_img0_), "is-live", TRUE, "format", GST_FORMAT_TIME, "do-timestamp", FALSE, "block",
                 TRUE, // backpressure instead of queue explosion
                 nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "is-live", TRUE, "format", GST_FORMAT_TIME, "do-timestamp", FALSE, "block", TRUE,
                 nullptr);
#endif
    g_object_set(appsink_img0_, "emit-signals", TRUE, "sync", FALSE, nullptr);
    g_object_set(appsink_img1_, "emit-signals", TRUE, "sync", FALSE, nullptr);

    g_signal_connect(appsink_img0_, "new-sample", G_CALLBACK(cb_new_sample_0), this);
    g_signal_connect(appsink_img1_, "new-sample", G_CALLBACK(cb_new_sample_1), this);

    pipeline_img0_ = gst_pipeline_new("pipeline_img0");
    pipeline_img1_ = gst_pipeline_new("pipeline_img1");

#if defined VIO
    gst_bin_add_many(GST_BIN(pipeline_img0_), appsrc_img0_, nvvideoconvert_0, encoder_img0, appsink_img0_, nullptr);
    gst_bin_add_many(GST_BIN(pipeline_img1_), appsrc_img1_, nvvideoconvert_1, encoder_img1, appsink_img1_, nullptr);

    // link elements
    if (!gst_element_link_many(appsrc_img0_, nvvideoconvert_0, encoder_img0, appsink_img0_, nullptr) ||
        !gst_element_link_many(appsrc_img1_, nvvideoconvert_1, encoder_img1, appsink_img1_, nullptr)) {
        abort("Failed to link elements");
    }
#elif defined ADA
    gst_bin_add_many(GST_BIN(pipeline_img0_), appsrc_img0_, vc_sys0, cf_i420_sys0, nvvideoconvert_0, cf_i420_nvmm0,
                     nvvideoconvert_0a, cf_nv24_nvmm0, encoder_img0, parse0, appsink_img0_, nullptr);

    gst_bin_add_many(GST_BIN(pipeline_img1_), appsrc_img1_, nvvideoconvert_1, encoder_img1, parse1, appsink_img1_, nullptr);

    bool ok  = gst_element_link_many(appsrc_img0_, vc_sys0, cf_i420_sys0, nvvideoconvert_0, cf_i420_nvmm0, nvvideoconvert_0a,
                                     cf_nv24_nvmm0, encoder_img0, parse0, appsink_img0_, nullptr);
    bool ok1 = gst_element_link_many(appsrc_img1_, nvvideoconvert_1, encoder_img1, parse1, appsink_img1_, nullptr);
    // auto L = [](GstElement* a, GstElement* b, const char* an, const char* bn) {
    //     gboolean ok = gst_element_link(a, b);
    //     if (!ok) {
    //         g_printerr("LINK FAIL: %s → %s\n", an, bn);
    //     }
    //     return ok;
    // };

    // bool ok = true;
    // ok = ok && L(appsrc_img0_, vc_sys0,           "appsrc0", "videoconvert_sys0");
    // ok = ok && L(vc_sys0,       cf_i420_sys0,      "videoconvert_sys0", "cf_i420_sys0");
    // ok = ok && L(cf_i420_sys0,  nvvideoconvert_0,  "cf_i420_sys0", "nvvideoconvert0");
    // ok = ok && L(nvvideoconvert_0, cf_i420_nvmm0,  "nvvideoconvert0", "cf_i420_nvmm0");
    // ok = ok && L(cf_i420_nvmm0, nvvideoconvert_0a, "cf_i420_nvmm0", "nvvideoconvert0a");
    // ok = ok && L(nvvideoconvert_0a, cf_nv24_nvmm0, "nvvideoconvert0a", "cf_nv24_nvmm0");
    // ok = ok && L(cf_nv24_nvmm0, encoder_img0,      "cf_nv24_nvmm0", "encoder_img0");
    // ok = ok && L(encoder_img0,  parse0,            "encoder_img0", "h265parse0");
    // ok = ok && L(parse0,        appsink_img0_,     "h265parse0", "appsink0");

    // if (!ok) abort("Failed to link elements (ADA pipeline, MSB).");
    if (!ok || !ok1) {
        if (!ok) {
            printf("Failed to link elements pipeline 0\n");
        }
        if (!ok1) {
            printf("Failed to link elements pipeline 1\n");
        }
        abort("can't link");
    }
#endif
    gst_element_set_state(pipeline_img0_, GST_STATE_PLAYING);
    gst_element_set_state(pipeline_img1_, GST_STATE_PLAYING);
}
