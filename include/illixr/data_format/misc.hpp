#pragma once
#if defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
#endif

#include "illixr/switchboard.hpp"

#ifdef __ANDROID__
#  include <android/hardware_buffer.h>
#  include <GLES/gl.h>
#else
#  include <GL/glu.h>
#endif

// Tell gldemo and timewarp_gl to use two texture handle for left and right eye
#define USE_ALT_EYE_FORMAT

using ullong = unsigned long long;

namespace ILLIXR::data_format {

struct [[maybe_unused]] connection_signal : public switchboard::event {
    bool start;

    explicit connection_signal(bool start_)
        : start{start_} { }
};

// Used to identify which graphics API is being used (for swapchain construction)
enum class graphics_api { OPENGL, VULKAN, TBD };

// Used to distinguish between different image handles
enum class swapchain_usage { LEFT_SWAPCHAIN, RIGHT_SWAPCHAIN, LEFT_RENDER, RIGHT_RENDER, NA };

typedef struct vk_image_handle {
    int      file_descriptor;
    int64_t  format;
    size_t   allocation_size;
    uint32_t width;
    uint32_t height;

    vk_image_handle(int fd_, int64_t format_, size_t alloc_size, uint32_t width_, uint32_t height_)
        : file_descriptor{fd_}
        , format{format_}
        , allocation_size{alloc_size}
        , width{width_}
        , height{height_} { }
} vk_image_handle;

#ifdef __ANDROID__
typedef struct vk_buffer_handle {
    AHardwareBuffer  *ahardware_buffer;
    int64_t  format;
    size_t   allocation_size;
    uint32_t width;
    uint32_t height;

    vk_buffer_handle(AHardwareBuffer *ahardware_buffer_, int64_t format_, size_t alloc_size, uint32_t width_, uint32_t height_)
            : ahardware_buffer{ahardware_buffer_}
            , format{format_}
            , allocation_size{alloc_size}
            , width{width_}
            , height{height_} { }
} vk_buffer_handle;

#endif
// This is used to share swapchain images between ILLIXR and Monado.
// When Monado uses its GL pipeline, it's enough to just share a context during creation.
// Otherwise, file descriptors are needed to share the images.
struct [[maybe_unused]] image_handle : public switchboard::event {
    graphics_api type;

    union {
        GLuint          gl_handle{};
        vk_image_handle vk_handle;
#ifdef __ANDROID__
        vk_buffer_handle vk_buffer_handle;
#endif
    };

    uint32_t        num_images;
    swapchain_usage usage;

    // This decides whether it's the left or right swapchain,
    // but it may be used in the future to support more composition layers as well.
    image_handle()
        : type{graphics_api::TBD}
        , gl_handle{0}
        , num_images{0}
        , usage{swapchain_usage::NA} { }

    image_handle(GLuint gl_handle_, uint32_t num_images_, swapchain_usage usage_)
        : type{graphics_api::OPENGL}
        , gl_handle{gl_handle_}
        , num_images{num_images_}
        , usage{usage_} { }

    image_handle(int vk_fd_, int64_t format, size_t alloc_size, uint32_t width_, uint32_t height_, uint32_t num_images_,
                 swapchain_usage usage_)
        : type{graphics_api::VULKAN}
        , vk_handle{vk_fd_, format, alloc_size, width_, height_}
        , num_images{num_images_}
        , usage{usage_} { }

#ifdef __ANDROID__
    image_handle(AHardwareBuffer *ahardware_buffer, int64_t format, size_t alloc_size, uint32_t width_, uint32_t height_, uint32_t num_images_,
                 swapchain_usage usage_)
            : type{graphics_api::VULKAN}
            , vk_buffer_handle{ahardware_buffer, format, alloc_size, width_, height_}
            , num_images{num_images_}
            , usage{usage_} { }

#endif
};

struct [[maybe_unused]] hologram_input : public switchboard::event {
    unsigned int seq{};

    hologram_input() = default;

    explicit hologram_input(unsigned int seq_)
        : seq{seq_} { }
};

struct [[maybe_unused]] signal_to_quad : public switchboard::event {
    ullong seq;

    explicit signal_to_quad(ullong seq_)
        : seq{seq_} { }
};

// High-level HMD specification, timewarp plugin
// may/will calculate additional HMD info based on these specifications
struct [[maybe_unused]] hmd_physical_info {
    float ipd;
    int   displayPixelsWide;
    int   displayPixelsHigh;
    float chromaticAberration[4];
    float K[11];
    int   visiblePixelsWide;
    int   visiblePixelsHigh;
    float visibleMetersWide;
    float visibleMetersHigh;
    float lensSeparationInMeters;
    float metersPerTanAngleAtCenter;
};

inline bool compare(const std::string& input, const std::string& val) {
    std::string v1 = input;
    std::string v2 = val;
    std::transform(v1.begin(), v1.end(), v1.begin(), ::tolower);
    std::transform(v2.begin(), v2.end(), v2.begin(), ::tolower);
    return v1 == v2;
}

#ifdef __ANDROID__
// Used to identify which graphics API is being used (for swapchain construction)
enum class semaphore_usage { LEFT_RENDER_COMPLETE, RIGHT_RENDER_COMPLETE, NA };

struct semaphore_handle : public switchboard::event {
    int vk_handle;
    semaphore_usage usage;

    semaphore_handle()
            : vk_handle{0}
            , usage{semaphore_usage::NA} {}

    semaphore_handle(int vk_handle_, semaphore_usage usage_)
            : vk_handle{vk_handle_}
            , usage{usage_} {}
};

struct [[maybe_unused]] illixr_signal : public switchboard::event {
    int illixr_ready;
    illixr_signal()
            : illixr_ready(false) {}

    explicit illixr_signal(int illixr_ready_)
            : illixr_ready(illixr_ready_) {}
};
#endif

} // namespace ILLIXR::data_format
