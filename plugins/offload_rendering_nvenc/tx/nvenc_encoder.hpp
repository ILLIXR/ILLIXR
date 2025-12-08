#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif
#include <cuda.h>
#include <future>
#include <nvEncodeAPI.h>
#include <opencv2/opencv.hpp>
#include <vector>

// Load NVENC API functions
typedef NVENCSTATUS(NVENCAPI* PNVENCODEAPICREATEINSTANCE)(NV_ENCODE_API_FUNCTION_LIST*);

namespace ILLIXR {
class nvenc_encoder {
public:
    [[maybe_unused]] nvenc_encoder(int w, int h, int bitrate = 10000000);

    std::vector<uint8_t>              encode(const cv::Mat& frame);
    std::future<std::vector<uint8_t>> encode_async(const cv::Mat& frame);
    ~nvenc_encoder();

private:
    static void check_nvenc(NVENCSTATUS status, const char* msg);
    static void check_cuda(CUresult result, const char* msg);
    //std::vector<uint8_t> extract_sps_pps(const std::vector<uint8_t>& data);
    void init_cuda();
    void init_nvenc();
    void init_encoder();
    void get_sequence_headers();
    void log_hevc_profile();
    void create_buffers();
    void convert_bgr_to_nv12(const cv::Mat& bgr, uint8_t* nv12, uint32_t pitch) const;
    void query_capabilities();

    void*                       encoder_           = nullptr;
    NV_ENCODE_API_FUNCTION_LIST nvenc_             = {NV_ENCODE_API_FUNCTION_LIST_VER};
    void*                       input_buffer_      = nullptr;
    NV_ENC_OUTPUT_PTR output_buffer_               = nullptr;
    CUcontext                   cu_context_        = nullptr;
    int                         width_;
    int height_;
    int bitrate_;
    int aligned_width_;
    int aligned_height_;

    bool                        initialized_       = false;
    std::vector<uint8_t> vps_sps_pps_;
    bool has_sps_pps_ = false;
    uint64_t frame_count_{0};
};
} // namespace ILLIXR
