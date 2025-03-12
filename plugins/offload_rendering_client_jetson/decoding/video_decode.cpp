/*
 * Copyright (c) 2019, The Board of Trustees of the University of Illinois. All rights reserved.
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

#include "video_decode.h"

#include "NvApplicationProfiler.h"
#include "NvUtils.h"

#include <bitset>
#include <cassert>
#include <chrono>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <linux/videodev2.h>
#include <malloc.h>
#include <poll.h>
#include <pthread.h>
#include <random>
#include <set>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#define TEST_ERROR(cond, str, label) \
    if (cond) {                      \
        cerr << str << endl;         \
        error = 1;                   \
        goto label;                  \
    }

#define MICROSECOND_UNIT 1000000
#define CHUNK_SIZE       4000000
#define MIN(a, b)        (((a) < (b)) ? (a) : (b))

#define IS_NAL_UNIT_START(buffer_ptr) (!buffer_ptr[0] && !buffer_ptr[1] && !buffer_ptr[2] && (buffer_ptr[3] == 1))

#define IS_NAL_UNIT_START1(buffer_ptr) (!buffer_ptr[0] && !buffer_ptr[1] && (buffer_ptr[2] == 1))

#define H264_NAL_UNIT_CODED_SLICE     1
#define H264_NAL_UNIT_CODED_SLICE_IDR 5

#define HEVC_NUT_TRAIL_N  0
#define HEVC_NUT_RASL_R   9
#define HEVC_NUT_BLA_W_LP 16
#define HEVC_NUT_CRA_NUT  21

#define IVF_FILE_HDR_SIZE  32
#define IVF_FRAME_HDR_SIZE 12

#define IS_H264_NAL_CODED_SLICE(buffer_ptr)     ((buffer_ptr[0] & 0x1F) == H264_NAL_UNIT_CODED_SLICE)
#define IS_H264_NAL_CODED_SLICE_IDR(buffer_ptr) ((buffer_ptr[0] & 0x1F) == H264_NAL_UNIT_CODED_SLICE_IDR)

#define IS_MJPEG_START(buffer_ptr) (buffer_ptr[0] == 0xFF && buffer_ptr[1] == 0xD8)
#define IS_MJPEG_END(buffer_ptr)   (buffer_ptr[0] == 0xFF && buffer_ptr[1] == 0xD9)

#define GET_H265_NAL_UNIT_TYPE(buffer_ptr) ((buffer_ptr[0] & 0x7E) >> 1)

using namespace std;

/**
 * Read the input NAL unit for h264/H265/Mpeg2/Mpeg4 decoder.
 *
 * @param stream            : Input stream
 * @param buffer            : NvBuffer pointer
 * @param parse_buffer      : parse buffer pointer
 * @param parse_buffer_size : chunk size
 * @param ctx               : Decoder context
 */
static int read_decoder_input_nalu(istream& stream, NvBuffer* buffer, char* parse_buffer, streamsize parse_buffer_size,
                                   context_t* ctx) {
    /* Length is the size of the buffer in bytes. */
    char*    buffer_ptr = (char*) buffer->planes[0].data;
    uint8_t  h265_nal_unit_type;
    char*    stream_ptr;
    bool     nalu_found = false;
    uint16_t h265_nal_unit_header;

    streamsize bytes_read;
    streamsize stream_initial_pos = stream.tellg();

    stream.read(parse_buffer, parse_buffer_size);
    bytes_read = stream.gcount();

    if (bytes_read == 0) {
        ctx->flag_copyts                   = false;
        return buffer->planes[0].bytesused = 0;
    }

    /* Find the first NAL unit in the buffer. */
    stream_ptr = parse_buffer;
    while ((stream_ptr - parse_buffer) < (bytes_read - 3)) {
        nalu_found = IS_NAL_UNIT_START(stream_ptr);
        if (nalu_found) {
            memcpy(buffer_ptr, stream_ptr, 4);
            buffer_ptr += 4;
            buffer->planes[0].bytesused = 4;
            stream_ptr += 4;
            break;
        }

        nalu_found = IS_NAL_UNIT_START1(stream_ptr);
        if (nalu_found) {
            memcpy(buffer_ptr, stream_ptr, 3);
            buffer_ptr += 3;
            buffer->planes[0].bytesused = 3;
            stream_ptr += 3;
            break;
        }
        stream_ptr++;
    }

    /* Reached end of buffer but could not find NAL unit. */
    if (!nalu_found) {
        ctx->flag_copyts = false;
        cerr << "Could not read nal unit from file. EOF or file corrupted" << endl;
        return -1;
    }

    if (ctx->copy_timestamp) {
        if (ctx->decoder_pixfmt == V4L2_PIX_FMT_H264) {
            if ((IS_H264_NAL_CODED_SLICE(stream_ptr)) || (IS_H264_NAL_CODED_SLICE_IDR(stream_ptr))) {
                /* Check for first_mb_in_slice parameter to find first slice of the video frame.
                 * If first_mb_in_slice equal to 0 then the current slice is the very first slice
                 * in a picture.
                 */
                ctx->flag_copyts = (stream_ptr[1] & 0x80) ? true : false;
            } else {
                ctx->flag_copyts = false;
            }
        } else if (ctx->decoder_pixfmt == V4L2_PIX_FMT_H265) {
            h265_nal_unit_header = (stream_ptr[0] << 8) | stream_ptr[1];
            h265_nal_unit_type   = (h265_nal_unit_header & 0x7e00) >> 9;

            if ((h265_nal_unit_type >= HEVC_NUT_TRAIL_N && h265_nal_unit_type <= HEVC_NUT_RASL_R) ||
                (h265_nal_unit_type >= HEVC_NUT_BLA_W_LP && h265_nal_unit_type <= HEVC_NUT_CRA_NUT)) {
                /* Check for first_slice_segment_in_pic_flag to find first slice of the video frame.
                 * first_slice_segment_in_pic_flag equal to 1 specifies that the slice segment is the
                 * first slice segment of the picture in decoding order.
                 */
                ctx->flag_copyts = (stream_ptr[2] >> 7) ? true : false;
            } else {
                ctx->flag_copyts = false;
            }
        }
    }

    /* Copy bytes till the next NAL unit is found. */
    while ((stream_ptr - parse_buffer) < (bytes_read - 3)) {
        if (IS_NAL_UNIT_START(stream_ptr) || IS_NAL_UNIT_START1(stream_ptr)) {
            streamsize seekto = stream_initial_pos + (stream_ptr - parse_buffer);
            if (stream.eof()) {
                stream.clear();
            }
            stream.seekg(seekto, stream.beg);
            return 0;
        }
        *buffer_ptr = *stream_ptr;
        buffer_ptr++;
        stream_ptr++;
        buffer->planes[0].bytesused++;
    }

    memcpy(buffer_ptr, stream_ptr, 3);
    buffer_ptr += 3;
    buffer->planes[0].bytesused += 3;
    stream_ptr += 3;

    /* Reached end of buffer but could not find NAL unit. */
    cerr << "Could not read nal unit from file. EOF or file corrupted" << endl;
    return -1;
}

/**
 * Read the input chunks for h264/H265/Mpeg2/Mpeg4 decoder.
 *
 * @param stream : Input stream
 * @param buffer : NvBuffer pointer
 */
static int read_decoder_input_chunk(istream* stream, NvBuffer* buffer) {
    /* Length is the size of the buffer in bytes */
    streamsize bytes_to_read = MIN(CHUNK_SIZE, buffer->planes[0].length);

    stream->read((char*) buffer->planes[0].data, bytes_to_read);
    /* NOTE: It is necessary to set bytesused properly, so that decoder knows how
             many bytes in the buffer are valid. */
    buffer->planes[0].bytesused = stream->gcount();
    if (buffer->planes[0].bytesused == 0) {
        stream->clear();
        stream->seekg(0, stream->beg);
    }
    return 0;
}

/**
 * Read the input chunks for MJPEG decoder.
 *
 * @param stream : Input filestream
 * @param buffer : NvBuffer pointer
 */
static int read_mjpeg_decoder_input(ifstream* stream, NvBuffer* buffer) {
    char* buffer_ptr = (char*) buffer->planes[0].data;
    stream->read(buffer_ptr, 2);
    buffer->planes[0].bytesused += stream->gcount();
    if (IS_MJPEG_START(buffer_ptr)) {
        while (!IS_MJPEG_END(buffer_ptr)) {
            buffer_ptr += 2;
            stream->read(buffer_ptr, 2);
            buffer->planes[0].bytesused += stream->gcount();
        }
    }

    if (buffer->planes[0].bytesused == 0) {
        stream->clear();
        stream->seekg(0, stream->beg);
    }

    return 0;
}

/**
 * Exit on error.
 *
 * @param ctx : Decoder context
 */
void mmapi_decoder::abort() {
    ctx.got_error = true;
    ctx.dec->abort();
}

/**
 * Report decoder input header error metadata.
 *
 * @param ctx             : Decoder context
 * @param input_metadata  : Pointer to decoder input header error metadata struct
 */
static int report_input_metadata(context_t* ctx, v4l2_ctrl_videodec_inputbuf_metadata* input_metadata) {
    int      ret       = -1;
    uint32_t frame_num = ctx->dec->output_plane.getTotalDequeuedBuffers() - 1;

    /* NOTE: Bits represent types of error as defined with v4l2_videodec_input_error_type. */
    if (input_metadata->nBitStreamError & V4L2_DEC_ERROR_SPS) {
        cout << "Frame " << frame_num << " BitStreamError : ERROR_SPS " << endl;
    } else if (input_metadata->nBitStreamError & V4L2_DEC_ERROR_PPS) {
        cout << "Frame " << frame_num << " BitStreamError : ERROR_PPS " << endl;
    } else if (input_metadata->nBitStreamError & V4L2_DEC_ERROR_SLICE_HDR) {
        cout << "Frame " << frame_num << " BitStreamError : ERROR_SLICE_HDR " << endl;
    } else if (input_metadata->nBitStreamError & V4L2_DEC_ERROR_MISSING_REF_FRAME) {
        cout << "Frame " << frame_num << " BitStreamError : ERROR_MISSING_REF_FRAME " << endl;
    } else if (input_metadata->nBitStreamError & V4L2_DEC_ERROR_VPS) {
        cout << "Frame " << frame_num << " BitStreamError : ERROR_VPS " << endl;
    } else {
        cout << "Frame " << frame_num << " BitStreamError : ERROR_None " << endl;
        ret = 0;
    }
    return ret;
}

/**
 * Report decoder output metadata.
 *
 * @param ctx      : Decoder context
 * @param metadata : Pointer to decoder output metadata struct
 */
static void report_metadata(context_t* ctx, v4l2_ctrl_videodec_outputbuf_metadata* metadata) {
    uint32_t frame_num = ctx->dec->capture_plane.getTotalDequeuedBuffers() - 1;

    cout << "Frame " << frame_num << endl;

    if (metadata->bValidFrameStatus) {
        if (ctx->decoder_pixfmt == V4L2_PIX_FMT_H264) {
            /* metadata for H264 input stream. */
            switch (metadata->CodecParams.H264DecParams.FrameType) {
            case 0:
                cout << "FrameType = B" << endl;
                break;
            case 1:
                cout << "FrameType = P" << endl;
                break;
            case 2:
                cout << "FrameType = I";
                if (metadata->CodecParams.H264DecParams.dpbInfo.currentFrame.bIdrFrame) {
                    cout << " (IDR)";
                }
                cout << endl;
                break;
            }
            cout << "nActiveRefFrames = " << metadata->CodecParams.H264DecParams.dpbInfo.nActiveRefFrames << endl;
        }

        if (ctx->decoder_pixfmt == V4L2_PIX_FMT_H265) {
            /* metadata for HEVC input stream. */
            switch (metadata->CodecParams.HEVCDecParams.FrameType) {
            case 0:
                cout << "FrameType = B" << endl;
                break;
            case 1:
                cout << "FrameType = P" << endl;
                break;
            case 2:
                cout << "FrameType = I";
                if (metadata->CodecParams.HEVCDecParams.dpbInfo.currentFrame.bIdrFrame) {
                    cout << " (IDR)";
                }
                cout << endl;
                break;
            }
            cout << "nActiveRefFrames = " << metadata->CodecParams.HEVCDecParams.dpbInfo.nActiveRefFrames << endl;
        }

        if (metadata->FrameDecStats.DecodeError) {
            /* decoder error status metadata. */
            v4l2_ctrl_videodec_statusmetadata* dec_stats = &metadata->FrameDecStats;
            cout << "ErrorType=" << dec_stats->DecodeError << " Decoded MBs=" << dec_stats->DecodedMBs
                 << " Concealed MBs=" << dec_stats->ConcealedMBs << endl;
        }
    } else {
        cout << "No valid metadata for frame" << endl;
    }
}

/**
 * Query and Set Capture plane.
 *
 * @param ctx : Decoder context
 */
void mmapi_decoder::query_and_set_capture() {
    NvVideoDecoder*                   dec = ctx.dec;
    struct v4l2_format                format;
    struct v4l2_crop                  crop;
    int32_t                           min_dec_capture_buffers;
    int                               ret   = 0;
    int                               error = 0;
    uint32_t                          window_width;
    uint32_t                          window_height;
    uint32_t                          sar_width;
    uint32_t                          sar_height;
    NvBufSurfaceColorFormat           pix_format;
    NvBufSurf::NvCommonAllocateParams params;
    NvBufSurf::NvCommonAllocateParams capParams;

    /* Get capture plane format from the decoder.
       This may change after resolution change event.
       Refer ioctl VIDIOC_G_FMT */
    ret = dec->capture_plane.getFormat(format);
    TEST_ERROR(ret < 0, "Error: Could not get format from decoder capture plane", error);

    /* Get the display resolution from the decoder.
       Refer ioctl VIDIOC_G_CROP */
    ret = dec->capture_plane.getCrop(crop);
    TEST_ERROR(ret < 0, "Error: Could not get crop from decoder capture plane", error);

    cout << "Video Resolution: " << crop.c.width << "x" << crop.c.height << endl;
    ctx.display_height = crop.c.height;
    ctx.display_width  = crop.c.width;

    /* Get the Sample Aspect Ratio (SAR) width and height */
    ret = dec->getSAR(sar_width, sar_height);
    cout << "Video SAR width: " << sar_width << " SAR height: " << sar_height << endl;
    if (ctx.dst_dma_fd != -1) {
        ret            = NvBufSurf::NvDestroy(ctx.dst_dma_fd);
        ctx.dst_dma_fd = -1;
        TEST_ERROR(ret < 0, "Error: Error in BufferDestroy", error);
    }
    /* Create output buffer for transform. */
    params.memType = NVBUF_MEM_SURFACE_ARRAY;
    params.width   = crop.c.width;
    params.height  = crop.c.height;
    if (ctx.vkRendering) {
        /* Foreign FD in rmapi_tegra is imported as block linear kind by default and
         * there is no way right now in our driver to know the kind at the time of
         * import
         * */
        params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
    } else
        params.layout = NVBUF_LAYOUT_PITCH;
    if (ctx.out_pixfmt == 1)
        params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
    else if (ctx.out_pixfmt == 2)
        params.colorFormat = NVBUF_COLOR_FORMAT_YUV420;
    else if (ctx.out_pixfmt == 3)
        params.colorFormat = NVBUF_COLOR_FORMAT_NV16;
    else if (ctx.out_pixfmt == 4)
        params.colorFormat = NVBUF_COLOR_FORMAT_NV24;

    params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;
    if (ctx.vkRendering)
        params.colorFormat = NVBUF_COLOR_FORMAT_RGBA;

    ret = NvBufSurf::NvAllocate(&params, 1, &ctx.dst_dma_fd);
    TEST_ERROR(ret == -1, "create dmabuf failed", error);

    if (!ctx.disable_rendering) {
        /* Destroy the old instance of renderer as resolution might have changed. */
        if (ctx.vkRendering) {
            // delete ctx.vkRenderer;
        } else {
            delete ctx.eglRenderer;
        }

        if (ctx.fullscreen) {
            /* Required for fullscreen. */
            window_width = window_height = 0;
        } else if (ctx.window_width && ctx.window_height) {
            /* As specified by user on commandline. */
            window_width  = ctx.window_width;
            window_height = ctx.window_height;
        } else {
            /* Resolution got from the decoder. */
            window_width  = crop.c.width;
            window_height = crop.c.height;
        }

        if (!ctx.vkRendering) {
            /* If height or width are set to zero, EglRenderer creates a fullscreen
               window for rendering. */
            ctx.eglRenderer =
                NvEglRenderer::createEglRenderer("renderer0", window_width, window_height, ctx.window_x, ctx.window_y);
            TEST_ERROR(!ctx.eglRenderer,
                       "Error in setting up of egl renderer. "
                       "Check if X is running or run with --disable-rendering",
                       error);
            if (ctx.stats) {
                /* Enable profiling for renderer if stats are requested. */
                ctx.eglRenderer->enableProfiling();
            }
            ctx.eglRenderer->setFPS(ctx.fps);
        } else {
            // ctx.vkRenderer =
            //     NvVulkanRenderer::createVulkanRenderer("renderer0", window_width, window_height, ctx.window_x, ctx.window_y);
            // TEST_ERROR(!ctx.vkRenderer,
            //            "Error in setting up of vulkan renderer. "
            //            "Check if X is running or run with --disable-rendering",
            //            error);
            // ctx.vkRenderer->setSize(window_width, window_height);
            // ctx.vkRenderer->initVulkan();
        }
    }

    /* deinitPlane unmaps the buffers and calls REQBUFS with count 0 */
    dec->capture_plane.deinitPlane();
    if (ctx.capture_plane_mem_type == V4L2_MEMORY_DMABUF) {
        for (int index = 0; index < ctx.numCapBuffers; index++) {
            if (ctx.dmabuff_fd[index] != 0) {
                ret = NvBufSurf::NvDestroy(ctx.dmabuff_fd[index]);
                TEST_ERROR(ret < 0, "Error: Error in BufferDestroy", error);
            }
        }
    }

    /* Not necessary to call VIDIOC_S_FMT on decoder capture plane.
       But decoder setCapturePlaneFormat function updates the class variables */
    ret = dec->setCapturePlaneFormat(format.fmt.pix_mp.pixelformat, format.fmt.pix_mp.width, format.fmt.pix_mp.height);
    TEST_ERROR(ret < 0, "Error in setting decoder capture plane format", error);

    ctx.video_height = format.fmt.pix_mp.height;
    ctx.video_width  = format.fmt.pix_mp.width;
    /* Get the minimum buffers which have to be requested on the capture plane. */
    ret = dec->getMinimumCapturePlaneBuffers(min_dec_capture_buffers);
    TEST_ERROR(ret < 0, "Error while getting value of minimum capture plane buffers", error);

    /* Request (min + extra) buffers, export and map buffers. */
    if (ctx.capture_plane_mem_type == V4L2_MEMORY_MMAP) {
        assert(false);
        /* Request, Query and export decoder capture plane buffers.
           Refer ioctl VIDIOC_REQBUFS, VIDIOC_QUERYBUF and VIDIOC_EXPBUF */
        ret =
            dec->capture_plane.setupPlane(V4L2_MEMORY_MMAP, min_dec_capture_buffers + ctx.extra_cap_plane_buffer, false, false);
        TEST_ERROR(ret < 0, "Error in decoder capture plane setup", error);
    } else if (ctx.capture_plane_mem_type == V4L2_MEMORY_DMABUF) {
        cout << "Decoder colorspace ITU-R BT.709 with extended range luma (0-255)" << endl;
        pix_format = NVBUF_COLOR_FORMAT_NV12_709_ER;

        ctx.numCapBuffers = min_dec_capture_buffers + ctx.extra_cap_plane_buffer;

        capParams.memType = NVBUF_MEM_SURFACE_ARRAY;
        capParams.width   = crop.c.width;
        capParams.height  = crop.c.height;
        capParams.layout  = NVBUF_LAYOUT_BLOCK_LINEAR;
        capParams.memtag  = NvBufSurfaceTag_VIDEO_DEC;

        if (format.fmt.pix_mp.pixelformat == V4L2_PIX_FMT_NV24M)
            pix_format = NVBUF_COLOR_FORMAT_NV24;
        else if (format.fmt.pix_mp.pixelformat == V4L2_PIX_FMT_NV24_10LE)
            pix_format = NVBUF_COLOR_FORMAT_NV24_10LE;
        if (ctx.decoder_pixfmt == V4L2_PIX_FMT_MJPEG) {
            capParams.layout = NVBUF_LAYOUT_PITCH;
            if (format.fmt.pix_mp.pixelformat == V4L2_PIX_FMT_YUV422M) {
                pix_format = NVBUF_COLOR_FORMAT_YUV422;
            } else {
                pix_format = NVBUF_COLOR_FORMAT_YUV420;
            }
        }

        capParams.colorFormat = pix_format;

        ret = NvBufSurf::NvAllocate(&capParams, ctx.numCapBuffers, ctx.dmabuff_fd);

        TEST_ERROR(ret < 0, "Failed to create buffers", error);
        /* Request buffers on decoder capture plane.
           Refer ioctl VIDIOC_REQBUFS */
        ret = dec->capture_plane.reqbufs(V4L2_MEMORY_DMABUF, ctx.numCapBuffers);
        TEST_ERROR(ret, "Error in request buffers on capture plane", error);
    }

    /* Decoder capture plane STREAMON.
       Refer ioctl VIDIOC_STREAMON */
    ret = dec->capture_plane.setStreamStatus(true);
    TEST_ERROR(ret < 0, "Error in decoder capture plane streamon", error);

    cout << "Number of capture plane buffers: " << dec->capture_plane.getNumBuffers() << endl;

    /* Enqueue all the empty decoder capture plane buffers. */
    for (uint32_t i = 0; i < dec->capture_plane.getNumBuffers(); i++) {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane  planes[MAX_PLANES];

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        v4l2_buf.index    = i;
        v4l2_buf.m.planes = planes;
        v4l2_buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_buf.memory   = ctx.capture_plane_mem_type;
        if (ctx.capture_plane_mem_type == V4L2_MEMORY_DMABUF)
            v4l2_buf.m.planes[0].m.fd = ctx.dmabuff_fd[i];
        ret = dec->capture_plane.qBuffer(v4l2_buf, NULL);
        TEST_ERROR(ret < 0, "Error Qing buffer at output plane", error);
    }
    cout << "Query and set capture successful" << endl;
    return;

error:
    if (error) {
        abort();
        cerr << "Error in " << __func__ << endl;
    }
}

/**
 * Decoder capture thread loop function.
 *
 * @param args : void arguments
 */
int mmapi_decoder::dec_capture(int dst_fd) {
    NvVideoDecoder*   dec = ctx.dec;
    struct v4l2_event ev;
    int               ret;

    if (!ctx.capture_ready) {
        /* Need to wait for the first Resolution change event, so that
           the decoder knows the stream resolution and can allocate appropriate
           buffers when we call REQBUFS. */
        do {
            /* Refer ioctl VIDIOC_DQEVENT */
            ret = dec->dqEvent(ev, 50000);
            if (ret < 0) {
                if (errno == EAGAIN) {
                    cerr << "Timed out waiting for first V4L2_EVENT_RESOLUTION_CHANGE" << endl;
                } else {
                    cerr << "Error in dequeueing decoder event" << endl;
                }
                abort();
                break;
            }
        } while ((ev.type != V4L2_EVENT_RESOLUTION_CHANGE) && !ctx.got_error);

        /* Received the resolution change event, now can do query_and_set_capture. */
        if (!ctx.got_error)
            query_and_set_capture();

        ctx.capture_ready = true;
    }

    if (ctx.got_error || dec->isInError()) {
        cout << "Decoder error or EOS" << endl;
        return -1;
    }

    /* Exit on error or EOS which is signalled in main() */
    NvBuffer* dec_buffer;

    /* Check for Resolution change again.
       Refer ioctl VIDIOC_DQEVENT */
    ret = dec->dqEvent(ev, false);
    if (ret == 0) {
        switch (ev.type) {
        case V4L2_EVENT_RESOLUTION_CHANGE:
            query_and_set_capture();
            return -EAGAIN;
        }
    }

    /* Decoder capture loop */
    struct v4l2_buffer v4l2_buf;
    struct v4l2_plane  planes[MAX_PLANES];

    memset(&v4l2_buf, 0, sizeof(v4l2_buf));
    memset(planes, 0, sizeof(planes));
    v4l2_buf.m.planes = planes;

    /* Dequeue a filled buffer. */
    if (dec->capture_plane.dqBuffer(v4l2_buf, &dec_buffer, NULL, 0)) {
        if (errno == EAGAIN) {
            if (v4l2_buf.flags & V4L2_BUF_FLAG_LAST) {
                cout << "Got EoS at capture plane" << endl;
                goto handle_eos;
            }
            usleep(1000);
            return -EAGAIN;
        } else {
            abort();
            cerr << "Error while calling dequeue at capture plane" << endl;
            throw std::runtime_error("Error while calling dequeue at capture plane");
        }
    }
    captureDQ++;
    // print fps every second
    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock ::now() - lastCaptureTime)
            .count() >= 1) {
        cout << "Capture FPS: " << captureDQ << endl;
        captureDQ       = 0;
        lastCaptureTime = std::chrono::high_resolution_clock ::now();
    }

    if (ctx.enable_metadata) {
        v4l2_ctrl_videodec_outputbuf_metadata dec_metadata;

        /* Get the decoder output metadata on capture-plane.
           Refer V4L2_CID_MPEG_VIDEODEC_METADATA */
        ret = dec->getMetadata(v4l2_buf.index, dec_metadata);
        if (ret == 0) {
            report_metadata(&ctx, &dec_metadata);
        }
    }

    if (ctx.copy_timestamp && ctx.input_nalu && ctx.stats) {
        cout << "[" << v4l2_buf.index
             << "]"
                "dec capture plane dqB timestamp ["
             << v4l2_buf.timestamp.tv_sec << "s" << v4l2_buf.timestamp.tv_usec << "us]" << endl;
    }

    if (!ctx.disable_rendering && ctx.stats && !ctx.vkRendering) {
        /* EglRenderer requires the fd of the 0th plane to render the buffer. */
        if (ctx.capture_plane_mem_type == V4L2_MEMORY_DMABUF)
            dec_buffer->planes[0].fd = ctx.dmabuff_fd[v4l2_buf.index];

        ctx.eglRenderer->render(dec_buffer->planes[0].fd);
    }

    if (ctx.out_file || (!ctx.disable_rendering && !ctx.stats)) {
        /* Clip & Stitch can be done by adjusting rectangle. */
        NvBufSurf::NvCommonTransformParams transform_params;
        transform_params.src_top    = 0;
        transform_params.src_left   = 0;
        transform_params.src_width  = ctx.display_width;
        transform_params.src_height = ctx.display_height;
        transform_params.dst_top    = 0;
        transform_params.dst_left   = 0;
        transform_params.dst_width  = ctx.display_width;
        transform_params.dst_height = ctx.display_height;
        transform_params.flag       = NVBUFSURF_TRANSFORM_FILTER;
        transform_params.flip       = NvBufSurfTransform_None;
        transform_params.filter     = NvBufSurfTransformInter_Nearest;
        if (ctx.capture_plane_mem_type == V4L2_MEMORY_DMABUF)
            dec_buffer->planes[0].fd = ctx.dmabuff_fd[v4l2_buf.index];
        /* Perform Blocklinear to PitchLinear conversion. */
        // std::cout << "plane fd" << dec_buffer->planes[0].fd << std::endl;
        ret = NvBufSurf::NvTransform(&transform_params, dec_buffer->planes[0].fd, dst_fd);
        if (ret == -1) {
            cerr << "Transform failed" << endl;
            throw std::runtime_error("Transform failed");
        }

        /* Write raw video frame to file. */
        if (!ctx.stats && ctx.out_file) {
            /* Dumping two planes for NV12, NV16, NV24 and three for I420 */
            dump_dmabuf(ctx.dst_dma_fd, 0, ctx.out_file);
            dump_dmabuf(ctx.dst_dma_fd, 1, ctx.out_file);
            if (ctx.out_pixfmt == 2) {
                dump_dmabuf(ctx.dst_dma_fd, 2, ctx.out_file);
            }
        }

        if (!ctx.stats && !ctx.disable_rendering) {
            if (ctx.vkRendering) {
            } else {
                ctx.eglRenderer->render(ctx.dst_dma_fd);
            }
        }

        /* If not writing to file, Queue the buffer back once it has been used. */
        if (ctx.capture_plane_mem_type == V4L2_MEMORY_DMABUF)
            v4l2_buf.m.planes[0].m.fd = ctx.dmabuff_fd[v4l2_buf.index];
        if (dec->capture_plane.qBuffer(v4l2_buf, NULL) < 0) {
            abort();
            cerr << "Error while queueing buffer at decoder capture plane" << endl;
            throw std::runtime_error("Error while queueing buffer at decoder capture plane");
        }
    } else {
        /* If not writing to file, Queue the buffer back once it has been used. */
        if (ctx.capture_plane_mem_type == V4L2_MEMORY_DMABUF)
            v4l2_buf.m.planes[0].m.fd = ctx.dmabuff_fd[v4l2_buf.index];
        if (dec->capture_plane.qBuffer(v4l2_buf, NULL) < 0) {
            abort();
            cerr << "Error while queueing buffer at decoder capture plane" << endl;
            throw std::runtime_error("Error while queueing buffer at decoder capture plane");
        }
    }
    return 0;
handle_eos:
    cout << "Decoder EOS" << endl;
    return -1;
}

/**
 * Set the default values for decoder context members.
 *
 * @param ctx : Decoder context
 */
static void set_defaults(context_t* ctx) {
    memset(ctx, 0, sizeof(context_t));
    ctx->fullscreen             = false;
    ctx->window_height          = 0;
    ctx->window_width           = 0;
    ctx->window_x               = 0;
    ctx->window_y               = 0;
    ctx->out_pixfmt             = 2;
    ctx->fps                    = 144;
    ctx->output_plane_mem_type  = V4L2_MEMORY_USERPTR;
    ctx->capture_plane_mem_type = V4L2_MEMORY_DMABUF;
    ctx->vp9_file_header_flag   = 0;
    ctx->vp8_file_header_flag   = 0;
    ctx->stress_test            = 1;
    ctx->copy_timestamp         = false;
    ctx->flag_copyts            = false;
    ctx->start_ts               = 0;
    ctx->file_count             = 1;
    ctx->dec_fps                = 144;
    ctx->dst_dma_fd             = -1;
    ctx->bLoop                  = false;
    ctx->bQueue                 = false;
    ctx->loop_count             = 0;
    ctx->max_perf               = 1;
    ctx->extra_cap_plane_buffer = 1;
    ctx->blocking_mode          = 1;
    ctx->vkRendering            = true;

    ctx->decoder_pixfmt = V4L2_PIX_FMT_H265; // default to H264
    ctx->input_nalu     = false;
    pthread_mutex_init(&ctx->queue_lock, NULL);
    pthread_cond_init(&ctx->queue_cond, NULL);
}

/**
 * Decode processing function for blocking mode.
 *
 * @param ctx               : Decoder context
 * @param eos               : end of stream
 * @param current_file      : current file
 * @param current_loop      : iterator count
 * @param nalu_parse_buffer : input parsed nal unit
 */
void mmapi_decoder::queue_output_plane_buffer(char* nalu_buffer, size_t nalu_size) {
    array_streambuf nalu_stream(nalu_buffer, nalu_size);
    auto            stm = istream(&nalu_stream);
    dec_internal(stm);
}

/**
 * Decode processing function.
 *
 * @param ctx  : Decoder context
 * @param argc : Argument Count
 * @param argv : Argument Vector
 */
int mmapi_decoder::decoder_init() {
    int                    ret          = 0;
    int                    error        = 0;
    uint32_t               current_file = 0;
    uint32_t               i;
    bool                   eos          = false;
    int                    current_loop = 0;
    NvApplicationProfiler& profiler     = NvApplicationProfiler::getProfilerInstance();

    /* Set default values for decoder context members. */
    set_defaults(&ctx);

    /* Set thread name for decoder Output Plane thread. */
    // pthread_setname_np(pthread_self(), "DecOutPlane");

    if (ctx.enable_sld && (ctx.decoder_pixfmt != V4L2_PIX_FMT_H265)) {
        fprintf(stdout, "Slice level decoding is only applicable for H265 so disabling it\n");
        ctx.enable_sld = false;
    }

    if (ctx.enable_sld && !ctx.input_nalu) {
        fprintf(stdout, "Enabling input nalu mode required for slice level decode\n");
        ctx.input_nalu = true;
    }

    /* Create NvVideoDecoder object for blocking or non-blocking I/O mode. */
    cout << "Creating decoder in blocking mode \n";
    // random number generator
    std::random_device              rd;
    std::mt19937                    gen(rd());
    std::uniform_int_distribution<> dis(0, 1000);
    ctx.dec = NvVideoDecoder::createVideoDecoder(std::to_string(dis(gen)).c_str());
    TEST_ERROR(!ctx.dec, "Could not create decoder", cleanup);

    /* Open the output file. */
    if (ctx.out_file_path) {
        ctx.out_file = new ofstream(ctx.out_file_path);
        TEST_ERROR(!ctx.out_file->is_open(), "Error opening output file", cleanup);
    }

    /* Enable profiling for decoder if stats are requested. */
    if (ctx.stats) {
        profiler.start(NvApplicationProfiler::DefaultSamplingInterval);
        ctx.dec->enableProfiling();
    }

    /* Subscribe to Resolution change event.
       Refer ioctl VIDIOC_SUBSCRIBE_EVENT */
    ret = ctx.dec->subscribeEvent(V4L2_EVENT_RESOLUTION_CHANGE, 0, 0);
    TEST_ERROR(ret < 0, "Could not subscribe to V4L2_EVENT_RESOLUTION_CHANGE", cleanup);

    /* Set format on the output plane.
       Refer ioctl VIDIOC_S_FMT */
    ret = ctx.dec->setOutputPlaneFormat(ctx.decoder_pixfmt, CHUNK_SIZE);
    TEST_ERROR(ret < 0, "Could not set output plane format", cleanup);

    /* Configure for frame input mode for decoder.
       Refer V4L2_CID_MPEG_VIDEO_DISABLE_COMPLETE_FRAME_INPUT */
    if (ctx.input_nalu) {
        /* Input to the decoder will be nal units. */
        nalu_parse_buffer = new char[CHUNK_SIZE];
        printf("Setting frame input mode to 1 \n");
        ret = ctx.dec->setFrameInputMode(1);
        TEST_ERROR(ret < 0, "Error in decoder setFrameInputMode", cleanup);
    } else {
        /* Input to the decoder will be a chunk of bytes.
           NOTE: Set V4L2_CID_MPEG_VIDEO_DISABLE_COMPLETE_FRAME_INPUT control to
                 false so that application can send chunks of encoded data instead
                 of forming complete frames. */
        printf("Setting frame input mode to 1 \n");
        ret = ctx.dec->setFrameInputMode(1);
        TEST_ERROR(ret < 0, "Error in decoder setFrameInputMode", cleanup);
    }

    if (ctx.enable_sld) {
        printf("Setting slice mode to 1 \n");
        ret = ctx.dec->setSliceMode(1);
        TEST_ERROR(ret < 0, "Error in decoder setSliceMode", cleanup);
    }

    /* Disable decoder DPB management.
       NOTE: V4L2_CID_MPEG_VIDEO_DISABLE_DPB should be set after output plane
             set format */
    if (ctx.disable_dpb) {
        ret = ctx.dec->disableDPB();
        TEST_ERROR(ret < 0, "Error in decoder disableDPB", cleanup);
    }

    /* Enable decoder error and metadata reporting.
       Refer V4L2_CID_MPEG_VIDEO_ERROR_REPORTING */
    if (ctx.enable_metadata || ctx.enable_input_metadata) {
        ret = ctx.dec->enableMetadataReporting();
        TEST_ERROR(ret < 0, "Error while enabling metadata reporting", cleanup);
    }

    /* Enable max performance mode by using decoder max clock settings.
       Refer V4L2_CID_MPEG_VIDEO_MAX_PERFORMANCE */
    if (ctx.max_perf) {
        ret = ctx.dec->setMaxPerfMode(ctx.max_perf);
        TEST_ERROR(ret < 0, "Error while setting decoder to max perf", cleanup);
    }

    /* Set the skip frames property of the decoder.
       Refer V4L2_CID_MPEG_VIDEO_SKIP_FRAMES */
    if (ctx.skip_frames) {
        ret = ctx.dec->setSkipFrames(ctx.skip_frames);
        TEST_ERROR(ret < 0, "Error while setting skip frames param", cleanup);
    }

    /* Query, Export and Map the output plane buffers so can read
       encoded data into the buffers. */
    if (ctx.output_plane_mem_type == V4L2_MEMORY_MMAP) {
        /* configure decoder output plane for MMAP io-mode.
           Refer ioctl VIDIOC_REQBUFS, VIDIOC_QUERYBUF and VIDIOC_EXPBUF */
        ret = ctx.dec->output_plane.setupPlane(V4L2_MEMORY_MMAP, 2, true, false);
    } else if (ctx.output_plane_mem_type == V4L2_MEMORY_USERPTR) {
        /* configure decoder output plane for USERPTR io-mode.
           Refer ioctl VIDIOC_REQBUFS */
        ret = ctx.dec->output_plane.setupPlane(V4L2_MEMORY_USERPTR, 10, false, true);
    }
    TEST_ERROR(ret < 0, "Error while setting up output plane", cleanup);

    /* Start stream processing on decoder output-plane.
       Refer ioctl VIDIOC_STREAMON */
    ret = ctx.dec->output_plane.setStreamStatus(true);
    TEST_ERROR(ret < 0, "Error in output plane stream on", cleanup);

    /* Enable copy timestamp with start timestamp in seconds for decode fps.
       NOTE: Used to demonstrate how timestamp can be associated with an
             individual H264/H265 frame to achieve video-synchronization. */
    // This shouldn't be used for offload rendering - Steven
    if (ctx.copy_timestamp && ctx.input_nalu) {
        ctx.timestamp     = (ctx.start_ts * MICROSECOND_UNIT);
        ctx.timestampincr = (MICROSECOND_UNIT * 16) / ((uint32_t) (ctx.dec_fps * 16));
    }

    // Print out number of capture plane buffers
    cout << "Number of output plane buffers: " << ctx.dec->output_plane.getNumBuffers() << endl;
    return 0;
cleanup:
    decoder_destroy();
    return -1;
}

int mmapi_decoder::decoder_destroy() {
    int ret   = 0;
    int error = 0;

    /* After sending EOS, all the buffers from output plane should be dequeued.
       and after that capture plane loop should be signalled to stop. */
    while (ctx.dec->output_plane.getNumQueuedBuffers() > 0 && !ctx.got_error && !ctx.dec->isInError()) {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane  planes[MAX_PLANES];

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        v4l2_buf.m.planes = planes;
        ret               = ctx.dec->output_plane.dqBuffer(v4l2_buf, NULL, NULL, -1);
        if (ret < 0) {
            cerr << "Error DQing buffer at output plane" << endl;
            abort();
            break;
        }
        if (v4l2_buf.m.planes[0].bytesused == 0) {
            cout << "Got EoS at output plane" << endl;
            break;
        }

        if ((v4l2_buf.flags & V4L2_BUF_FLAG_ERROR) && ctx.enable_input_metadata) {
            v4l2_ctrl_videodec_inputbuf_metadata dec_input_metadata;
            /* Get the decoder input metadata.
               Refer V4L2_CID_MPEG_VIDEODEC_INPUT_METADATA */
            ret = ctx.dec->getInputMetadata(v4l2_buf.index, dec_input_metadata);
            if (ret == 0) {
                ret = report_input_metadata(&ctx, &dec_input_metadata);
                if (ret == -1) {
                    cerr << "Error with input stream header parsing" << endl;
                    abort();
                    break;
                }
            }
        }
    }

    /* Signal EOS to the decoder capture loop. */
    ctx.got_eos = true;

cleanup:
    if (ctx.dec_capture_loop) {
        pthread_join(ctx.dec_capture_loop, NULL);
    }

    if (ctx.capture_plane_mem_type == V4L2_MEMORY_DMABUF) {
        for (int index = 0; index < ctx.numCapBuffers; index++) {
            if (ctx.dmabuff_fd[index] != 0) {
                ret = NvBufSurf::NvDestroy(ctx.dmabuff_fd[index]);
                if (ret < 0) {
                    cerr << "Failed to Destroy NvBuffer" << endl;
                }
            }
        }
    }
    if (ctx.dec && ctx.dec->isInError()) {
        cerr << "Decoder is in error" << endl;
        error = 1;
    }

    if (ctx.got_error) {
        error = 1;
    }

    /* The decoder destructor does all the cleanup i.e set streamoff on output and
       capture planes, unmap buffers, tell decoder to deallocate buffer (reqbufs
       ioctl with count = 0), and finally call v4l2_close on the fd. */
    delete ctx.dec;
    /* Similarly, Renderer destructor does all the cleanup. */
    if (ctx.vkRendering) {
    } else {
        delete ctx.eglRenderer;
    }
    for (uint32_t i = 0; i < ctx.file_count; i++)
        delete ctx.in_file[i];
    delete ctx.out_file;
    if (ctx.dst_dma_fd != -1) {
        ret            = NvBufSurf::NvDestroy(ctx.dst_dma_fd);
        ctx.dst_dma_fd = -1;
        if (ret < 0) {
            cerr << "Error in BufferDestroy" << endl;
            error = 1;
        }
    }
    delete[] nalu_parse_buffer;

    free(ctx.in_file);
    for (uint32_t i = 0; i < ctx.file_count; i++)
        free(ctx.in_file_path[i]);
    free(ctx.in_file_path);
    free(ctx.out_file_path);

    return -error;
}

bool mmapi_decoder::dec_internal(std::istream& istream) {
    bool               eos = false;
    int                ret = 0;
    struct v4l2_buffer temp_buf;

    /* Since all the output plane buffers have been queued, we first need to
       dequeue a buffer from output plane before we can read new data into it
       and queue it again. */
    if (ctx.got_error || ctx.dec->isInError()) {
        return true;
    }
    struct v4l2_buffer v4l2_buf;
    struct v4l2_plane  planes[MAX_PLANES];
    NvBuffer*          buffer;

    memset(&v4l2_buf, 0, sizeof(v4l2_buf));
    memset(planes, 0, sizeof(planes));

    v4l2_buf.m.planes = planes;

    /* dequeue a buffer for output plane. */
    if (i_output_plane_buf_filled >= ctx.dec->output_plane.getNumBuffers()) {
        ret = ctx.dec->output_plane.dqBuffer(v4l2_buf, &buffer, NULL, -1);
        if (ret < 0) {
            cerr << "Error DQing buffer at output plane" << endl;
            abort();
            throw std::runtime_error("Error DQing buffer at output plane");
        }
    } else {
        v4l2_buf.index = i_output_plane_buf_filled;
        buffer         = ctx.dec->output_plane.getNthBuffer(i_output_plane_buf_filled++);
    }

    if ((v4l2_buf.flags & V4L2_BUF_FLAG_ERROR) && ctx.enable_input_metadata) {
        v4l2_ctrl_videodec_inputbuf_metadata dec_input_metadata;

        /* Get the decoder input metadata.
           Refer V4L2_CID_MPEG_VIDEODEC_INPUT_METADATA */
        ret = ctx.dec->getInputMetadata(v4l2_buf.index, dec_input_metadata);
        if (ret == 0) {
            ret = report_input_metadata(&ctx, &dec_input_metadata);
            if (ret == -1) {
                cerr << "Error with input stream header parsing" << endl;
            }
        }
    }

    if ((ctx.decoder_pixfmt == V4L2_PIX_FMT_H264) || (ctx.decoder_pixfmt == V4L2_PIX_FMT_H265) ||
        (ctx.decoder_pixfmt == V4L2_PIX_FMT_MPEG2) || (ctx.decoder_pixfmt == V4L2_PIX_FMT_MPEG4)) {
        if (ctx.input_nalu) {
        } else {
            /* read the input chunks. */
            read_decoder_input_chunk(&istream, buffer);
        }
    }

    v4l2_buf.m.planes[0].bytesused = buffer->planes[0].bytesused;

    if (ctx.input_nalu && ctx.copy_timestamp) {
        /* Update the timestamp. */
        v4l2_buf.flags |= V4L2_BUF_FLAG_TIMESTAMP_COPY;
        if (ctx.flag_copyts)
            ctx.timestamp += ctx.timestampincr;
        v4l2_buf.timestamp.tv_sec  = ctx.timestamp / (MICROSECOND_UNIT);
        v4l2_buf.timestamp.tv_usec = ctx.timestamp % (MICROSECOND_UNIT);
    }

    if (ctx.copy_timestamp && ctx.input_nalu && ctx.stats) {
        cout << "[" << v4l2_buf.index
             << "]"
                "dec output plane qB timestamp ["
             << v4l2_buf.timestamp.tv_sec << "s" << v4l2_buf.timestamp.tv_usec << "us]" << endl;
    }

    /* enqueue a buffer for output plane. */
    ret = ctx.dec->output_plane.qBuffer(v4l2_buf, NULL);
    outputQ++;
    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock ::now() - lastOutputTime).count() >=
        1) {
        cout << "Output FPS: " << outputQ << endl;
        outputQ        = 0;
        lastOutputTime = std::chrono::high_resolution_clock ::now();
    }

    if (ret < 0) {
        cerr << "Error Qing buffer at output plane" << endl;
        abort();
        throw std::runtime_error("Error Qing buffer at output plane");
    }
    if (v4l2_buf.m.planes[0].bytesused == 0) {
        eos = true;
        cout << "Input file read complete" << endl;
    }
    return eos;
}
