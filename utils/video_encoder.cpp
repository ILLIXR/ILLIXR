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

    auto nvvideoconvert_0 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert0");
    auto nvvideoconvert_1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1");

    auto encoder_img0 = gst_element_factory_make(ILLIXR_ENCODING, "encoder_img0");
    auto encoder_img1 = gst_element_factory_make(ILLIXR_ENCODING, "encoder_img1");

    auto caps_8uc1 =
            gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "GRAY8", "framerate", GST_TYPE_FRACTION, TYPE_FRACTION,
                                1, "width", G_TYPE_INT, IMG_WIDTH, "height", G_TYPE_INT, IMG_HEIGHT, NULL);

    g_object_set(G_OBJECT(appsrc_img0_), "caps", caps_8uc1, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "caps", caps_8uc1, nullptr);
    gst_caps_unref(caps_8uc1);

    // set bitrate from environment variables
    // g_object_set(G_OBJECT(encoder_img0), "bitrate", std::stoi(sb->get_env("ILLIXR_BITRATE")), nullptr, 10);
    // g_object_set(G_OBJECT(encoder_img1), "bitrate", std::stoi(sb->get_env("ILLIXR_BITRATE")), nullptr, 10);

    // set bitrate from defined variables
    g_object_set(G_OBJECT(encoder_img1), "bitrate", ILLIXR_BITRATE, nullptr);
#if defined VIO
    g_object_set(G_OBJECT(encoder_img0), "bitrate", ILLIXR_BITRATE, nullptr);
    g_object_set(G_OBJECT(appsrc_img0_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);
    g_object_set(G_OBJECT(appsrc_img1_), "stream-type", 0, "format", GST_FORMAT_BYTES, "is-live", TRUE, nullptr);
#elif defined ADA
    // this is for 4(lossless setting) 2(default)
    g_object_set(G_OBJECT(encoder_img0), "tuning-info-id", 4, nullptr);
    g_object_set(G_OBJECT(encoder_img1), "tuning-info-id", 2, nullptr);

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

    gst_bin_add_many(GST_BIN(pipeline_img0_), appsrc_img0_, nvvideoconvert_0, encoder_img0, appsink_img0_, nullptr);
    gst_bin_add_many(GST_BIN(pipeline_img1_), appsrc_img1_, nvvideoconvert_1, encoder_img1, appsink_img1_, nullptr);

    // link elements
    if (!gst_element_link_many(appsrc_img0_, nvvideoconvert_0, encoder_img0, appsink_img0_, nullptr) ||
        !gst_element_link_many(appsrc_img1_, nvvideoconvert_1, encoder_img1, appsink_img1_, nullptr)) {
        abort("Failed to link elements");
    }

    gst_element_set_state(pipeline_img0_, GST_STATE_PLAYING);
    gst_element_set_state(pipeline_img1_, GST_STATE_PLAYING);
}
