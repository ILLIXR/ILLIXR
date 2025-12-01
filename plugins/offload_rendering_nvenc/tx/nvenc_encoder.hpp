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
    [[maybe_unused]] nvenc_encoder(int w, int h);

    std::vector<uint8_t>              encode(const cv::Mat& frame);
    std::future<std::vector<uint8_t>> encode_async(const cv::Mat& frame);
    ~nvenc_encoder();

private:
    static void check_nvenc(NVENCSTATUS status, const char* msg);
    static void check_cuda(CUresult result, const char* msg);

    void*                       encoder_           = nullptr;
    NV_ENCODE_API_FUNCTION_LIST nvenc_             = {NV_ENCODE_API_FUNCTION_LIST_VER};
    void*                       input_buffer_      = nullptr;
    void*                       output_bit_stream_ = nullptr;
    CUcontext                   cu_context_        = nullptr;
    int                         width_, height_;
    bool                        initialized_       = false;
};
} // namespace ILLIXR
