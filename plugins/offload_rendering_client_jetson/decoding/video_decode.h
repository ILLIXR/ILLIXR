/*
 * Copyright (c) 2016-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "NvBufSurface.h"
#include "NvEglRenderer.h"
#include "NvVideoDecoder.h"
// #include "NvVulkanRenderer.h"

#include <chrono>
#include <fstream>
#include <functional>
#include <pthread.h>
#include <queue>
#include <semaphore.h>

#define MAX_BUFFERS 32

typedef struct {
    NvVideoDecoder* dec;
    uint32_t        decoder_pixfmt;

    NvEglRenderer* eglRenderer;
    // NvVulkanRenderer* vkRenderer;
    bool vkRendering;

    char**          in_file_path;
    std::ifstream** in_file;

    char*          out_file_path;
    std::ofstream* out_file;

    bool     disable_rendering;
    bool     fullscreen;
    uint32_t window_height;
    uint32_t window_width;
    uint32_t window_x;
    uint32_t window_y;
    uint32_t out_pixfmt;
    uint32_t video_height;
    uint32_t video_width;
    uint32_t display_height;
    uint32_t display_width;
    uint32_t file_count;
    float    fps;

    bool disable_dpb;

    bool input_nalu;
    bool enable_sld;

    bool     copy_timestamp;
    bool     flag_copyts;
    uint32_t start_ts;
    float    dec_fps;
    uint64_t timestamp;
    uint64_t timestampincr;

    bool stats;

    int                        stress_test;
    bool                       enable_metadata;
    bool                       bLoop;
    bool                       bQueue;
    bool                       enable_input_metadata;
    enum v4l2_skip_frames_type skip_frames;
    enum v4l2_memory           output_plane_mem_type;
    enum v4l2_memory           capture_plane_mem_type;

    std::queue<NvBuffer*>* conv_output_plane_buf_queue;
    pthread_mutex_t        queue_lock;
    pthread_cond_t         queue_cond;

    pthread_t dec_pollthread; // Polling thread, created if running in non-blocking mode.

    pthread_t dec_capture_loop; // Decoder capture thread, created if running in blocking mode.
    bool      got_error;
    bool      got_eos;
    bool      vp9_file_header_flag;
    bool      vp8_file_header_flag;
    int       dst_dma_fd;
    int       dmabuff_fd[MAX_BUFFERS];
    int       numCapBuffers;
    int       loop_count;
    int       max_perf;
    int       extra_cap_plane_buffer;
    int       blocking_mode; // Set to true if running in blocking mode

    bool capture_ready;
} context_t;

class mmapi_decoder {
public:
    class array_streambuf : public std::streambuf {
    public:
        array_streambuf(char* begin, size_t length) {
            // Set the current get pointer to the start of the buffer and the end get pointer to the end of the buffer.
            this->setg(begin, begin, begin + length);
        }
    };

    mmapi_decoder() = default;

    int  decoder_init();
    int  dec_capture(int dst_fd);
    int  decoder_destroy();
    void queue_output_plane_buffer(char* nalu_buffer, size_t nalu_size);
    void query_and_set_capture();
    void abort();

private:
    context_t ctx;
    char*     nalu_parse_buffer         = NULL;
    int       i_output_plane_buf_filled = 0;
    bool      dec_internal(std::istream& istream);

    int                                                         outputQ         = 0;
    int                                                         captureDQ       = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastOutputTime  = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> lastCaptureTime = std::chrono::high_resolution_clock::now();
};