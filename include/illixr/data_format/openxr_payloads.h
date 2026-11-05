#pragma once

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Topic id's
// When adding new topics always increment from the last number used, these values are used as indexes and must start from 0
#define IXR_TOPIC_FOO   0
#define IXR_TOPIC_FOO_ECHO   1
#define IXR_TOPIC_AUDIO_CAPTURE  2 // App -> ILLIXR, switchboard topic "unity_audio_capture"
#define IXR_TOPIC_AUDIO_INPUT    3 // ILLIXR -> App, switchboard topic "unity_audio_playback"
#define IXR_TOPIC_COUNT          4

// Define data types below
// Each type must have its own unique identifier and a version number
// When adding new data types always increment the data type identifier from the last number used, do not reuse any numbers
#define IXR_DATA_TYPE_FOO     1
#define IXR_FOO_DATA_VERSION  1

typedef struct ixr_foo_data {
    float   values[4];
    int32_t count;
    int32_t padding;
} ixr_foo_data;

/*
 * Audio. Interleaved float samples in [-1, 1], frame_count * channel_count of them are valid.
 * start_frame is the stream index of the chunk's first frame; consecutive chunks are
 * contiguous, so a jump means frames were lost.
 */

#define IXR_DATA_TYPE_AUDIO_CHUNK    3
#define IXR_AUDIO_CHUNK_DATA_VERSION 1

#define IXR_AUDIO_MAX_CHANNELS 2
#define IXR_AUDIO_MAX_FRAMES   1024

typedef struct ixr_audio_chunk {
    uint64_t start_frame;
    uint32_t sample_rate;
    uint32_t channel_count;
    uint32_t frame_count;
    uint32_t reserved;
    float    samples[IXR_AUDIO_MAX_FRAMES * IXR_AUDIO_MAX_CHANNELS];
} ixr_audio_chunk;

#ifdef __cplusplus
};
#endif
