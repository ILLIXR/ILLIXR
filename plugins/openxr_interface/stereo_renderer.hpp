#pragma once

#include "illixr/data_format/frame.hpp"
#include "illixr/quest3_params.hpp"

#include <android/hardware_buffer.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

namespace ILLIXR {

/**
 * @brief Stereo video renderer for Quest 3 — Vulkan backend.
 *
 * Replaces the previous OpenGL ES / SurfaceTexture path entirely.
 * No EGL context is needed; this class operates purely on Vulkan objects.
 *
 * Typical per-frame usage (called from the render thread in oxr_interface):
 *
 *   renderer_->receive_frame(frame);          // import AHardwareBuffers
 *   for (int eye = 0; eye < 2; eye++) {
 *       renderer_->render_eye(eye,            // record + submit command buffer
 *                             swapchain_image,
 *                             swapchain_width, swapchain_height,
 *                             signal_semaphore);
 *
 *       if (frame.has_valid_depth()) {
 *           renderer_->render_eye_depth(eye, ...);    // depth swapchain image
 *       }
 *
 *       if (frame.has_valid_motion_vectors()) {
 *           renderer_->render_eye_motion_vec(eye, …); // spacewarp MV swapchain
 *       }
 *   }
 */
class stereo_renderer {
public:
    stereo_renderer() = default;
    ~stereo_renderer();

    // Non-copyable
    stereo_renderer(const stereo_renderer&)            = delete;
    stereo_renderer& operator=(const stereo_renderer&) = delete;

    /**
     * @brief Initialize Vulkan rendering resources.
     *
     * Must be called once after the OpenXR Vulkan session has been created.
     *
     * @param instance         Vulkan instance (created via xrCreateVulkanInstanceKHR).
     * @param physical_device  Physical device selected by OpenXR.
     * @param device           Logical device (created via xrCreateVulkanDeviceKHR).
     * @param queue            Graphics queue used for rendering submissions.
     * @param queue_family     Queue family index for the graphics queue.
     * @param swapchain_format VkFormat of the OpenXR swapchain images.
     * @return true on success.
     */
    bool initialize(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VkQueue queue,
                    uint32_t queue_family, VkFormat swapchain_format);

    /**
     * @brief Import AHardwareBuffer handles from a decoded frame into Vulkan.
     *
     * Call this once per frame before any render_eye* calls.
     * For hardware_buffer format: caches imported images keyed by buffer pointer.
     * For nv12 format: uploads pixel data to internal textures.
     */
    void receive_frame(const data_format::dual_frames& frame);

    /**
     * @brief Record and submit a command buffer that renders one eye's color.
     *
     * @param eye              0 = left, 1 = right.
     * @param swapchain_image  The VkImage from xrAcquireSwapchainImage.
     * @param swapchain_width  Swapchain image width.
     * @param swapchain_height Swapchain image height.
     * @param signal_semaphore VkSemaphore to signal when GPU work is complete
     *                         (pass VK_NULL_HANDLE if not needed).
     * @return true on success.
     */
    bool render_eye(int eye, VkImage swapchain_image, uint32_t swapchain_width, uint32_t swapchain_height,
                    VkSemaphore signal_semaphore = VK_NULL_HANDLE);

    /**
     * @brief Record and submit a command buffer that renders one eye's depth.
     *
     * Samples the decoded depth AHardwareBuffer (RG-packed 16-bit depth) and
     * writes the reconstructed normalised depth values into the OpenXR depth
     * swapchain image.  Must only be called when has_frame() is true and the
     * frame has has_valid_depth() == true.
     *
     * @param eye                  0 = left, 1 = right.
     * @param depth_swapchain_image The VkImage from the depth XrSwapchain.
     * @param depth_format         VkFormat of the depth swapchain (e.g. VK_FORMAT_D32_SFLOAT).
     * @param swapchain_width      Image width.
     * @param swapchain_height     Image height.
     * @return true on success.
     */
    bool render_eye_depth(int eye, VkImage depth_swapchain_image, VkFormat depth_format, uint32_t swapchain_width,
                          uint32_t swapchain_height);
    /**
     * @brief Record and submit a command buffer that renders one eye's
     *        motion-vector data into an App Spacewarp swapchain image.
     *
     * Samples the decoded motion-vector AHardwareBuffer (HEVC 10-bit,
     * two-component screen-space motion packed into Y/Cb planes) and converts
     * it to VK_FORMAT_R16G16B16A16_SFLOAT for submission via
     * XrCompositionLayerSpaceWarpInfoFB::motionVectorSubImage.
     *
     * Must only be called when has_frame() is true and the frame has
     * has_valid_motion_vectors() == true.
     *
     * @param eye              0 = left, 1 = right.
     * @param mv_swapchain_image The VkImage from the motion-vector XrSwapchain.
     * @param swapchain_width  Image width  (typically 432).
     * @param swapchain_height Image height (typically 432).
     * @return true on success.
     */
    bool render_eye_motion_vec(int eye, VkImage mv_swapchain_image, uint32_t swapchain_width, uint32_t swapchain_height);
    /**
     * @brief Wait for all in-flight rendering to complete.
     *
     * Call before releasing AHardwareBuffers from the previous frame.
     */
    void wait_idle();

    // Check if renderer is initialized
    [[nodiscard]] bool is_initialized() const {
        return initialized_;
    }

    // Check if we have a valid frame to render
    [[nodiscard]] bool has_frame() const {
        return has_valid_frame_;
    }

    void cleanup();

    // Set the crop dimensions (original size before padding)
    // Call this before initialize() or after receiving frame metadata
    void set_crop_region(int original_width, int original_height, int padded_width, int padded_height);

#ifdef COMBINED_ENCODING
    /**
     * @brief Signal that color frames arrive as side-by-side combined images.
     *
     * Under COMBINED_ENCODING the server encodes both eyes in a single wide
     * AHardwareBuffer.  Both left_eye and right_eye in the dual_frames point
     * to the same buffer; render_eye() samples the left half (U in [0, 0.5])
     * for eye 0 and the right half (U in [0.5, 1.0]) for eye 1.
     *
     * The vertex shader must accept a third push-constant float (u_offset) at
     * byte offset 8.  The shader generates UVs as:
     *   u = u_offset + frag_u * crop_scale_x
     *   v = (1 - frag_v) * crop_scale_y
     * where crop_scale_x = 0.5 * (original_width / padded_half_width).
     */
    void set_combined_encoding(bool enabled) {
        combined_encoding_ = enabled;
    }
#endif

private:
    // ── Imported decoder image (one per unique AHardwareBuffer pointer) ────────
    struct imported_image {
        VkImage                  image        = VK_NULL_HANDLE;
        VkDeviceMemory           memory       = VK_NULL_HANDLE;
        VkImageView              image_view   = VK_NULL_HANDLE;
        VkSamplerYcbcrConversion ycbcr_conv   = VK_NULL_HANDLE;
        VkSampler                sampler      = VK_NULL_HANDLE;
        uint64_t                 external_fmt = 0; // from AHardwareBufferProperties
        AHardwareBuffer*         hw_buffer    = nullptr;
    };

    // ── Shader compilation ────────────────────────────────────────────────────
    static VkShaderModule create_shader_module(VkDevice device, const uint32_t* spv, size_t word_count);

    // ── Per-eye descriptor setup ───────────────────────────────────────────────
    bool update_descriptor_set(int eye, const imported_image& img);
    bool update_depth_descriptor_set(int eye, const imported_image& img);
    bool update_mv_descriptor_set(int eye, const imported_image& img);

    // ── AHardwareBuffer → Vulkan import ────────────────────────────────────────
    imported_image* import_hardware_buffer(AHardwareBuffer* hw_buffer);
    void            destroy_imported_image(imported_image& img);

    imported_image* import_mv_hardware_buffer(AHardwareBuffer* hw_buffer);

    // ── Color pipeline helpers ─────────────────────────────────────────────────
    bool create_render_pass();
    bool create_pipeline(const imported_image& prototype);
    bool create_command_pool();
    bool allocate_command_buffers();
    bool create_descriptor_pool();

    // ── Boba vector/modal overlay helpers ────────────────────────────────────
    struct overlay_vertex {
        float x;
        float y;
        float red;
        float green;
        float blue;
        float alpha;
    };

    struct modal_vertex {
        float x;
        float y;
        float u;
        float v;
    };

    bool create_boba_overlay_resources();
    bool create_overlay_pipeline();
    bool create_modal_pipeline();
    bool create_host_visible_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* buffer,
                                    VkDeviceMemory* memory, void** mapped);
    bool upload_modal_texture(std::uint64_t texture_id, std::uint32_t width, std::uint32_t height,
                              const std::vector<std::uint8_t>& rgba);
    void destroy_modal_texture();
    void update_boba_overlay_state(const data_format::dual_frames& frame);
    void record_boba_overlays(VkCommandBuffer command_buffer, int eye);
    std::uint32_t find_memory_type(std::uint32_t type_filter, VkMemoryPropertyFlags properties) const;

    // ── Depth pipeline helpers ─────────────────────────────────────────────────
    bool create_depth_render_pass(VkFormat depth_format);
    bool create_depth_pipeline(const imported_image& prototype, VkFormat depth_format);
    bool create_depth_descriptor_pool();
    bool allocate_depth_command_buffers();

    // ── Motion-vector pipeline helpers ────────────────────────────────────────
    /// Create the VK_FORMAT_R16G16B16A16_SFLOAT color render pass used when
    /// writing App Spacewarp motion vectors.
    bool create_mv_render_pass();

    /// Create the graphics pipeline that samples a YCbCr AHardwareBuffer and
    /// converts the packed motion-vector data to R16G16B16A16_SFLOAT.
    /// @param prototype An already-imported AHardwareBuffer image whose
    ///                  YCbCr conversion info is used to build the immutable sampler.
    bool create_mv_pipeline(const imported_image& prototype);

    bool create_mv_descriptor_pool();
    bool allocate_mv_command_buffers();

    // Vulkan device objects (not owned — owned by oxr_interface)
    VkInstance       instance_         = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_  = VK_NULL_HANDLE;
    VkDevice         device_           = VK_NULL_HANDLE;
    VkQueue          queue_            = VK_NULL_HANDLE;
    uint32_t         queue_family_     = 0;
    VkFormat         swapchain_format_ = VK_FORMAT_R8G8B8A8_UNORM;

    // Color rendering resources (owned)
    VkRenderPass          render_pass_     = VK_NULL_HANDLE;
    VkPipelineLayout      pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline            pipeline_        = VK_NULL_HANDLE;
    VkDescriptorPool      descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_set_layout_ = VK_NULL_HANDLE;
    VkCommandPool         command_pool_    = VK_NULL_HANDLE;

    // Per-eye color resources
    std::array<VkDescriptorSet, 2> descriptor_sets_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkCommandBuffer, 2> command_buffers_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkFence, 2>         render_fences_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    // Transient per-frame objects from the previous render_eye() call.
    // Destroyed at the top of the next call, after the fence has signalled,
    // because Vulkan requires them to outlive their command buffer submission.
    std::array<VkFramebuffer, 2> prev_framebuffers_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkImageView, 2>   prev_swapchain_views_{VK_NULL_HANDLE, VK_NULL_HANDLE};

    // Boba vector overlays (controller rays, placement rectangle, markers).
    VkPipelineLayout overlay_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       overlay_pipeline_        = VK_NULL_HANDLE;
    std::array<VkBuffer, 2>       overlay_vertex_buffers_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkDeviceMemory, 2> overlay_vertex_memories_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<void*, 2>          overlay_vertex_mapped_{nullptr, nullptr};
    std::array<std::vector<overlay_vertex>, 2> overlay_vertices_{};

    // Boba modal bitmap card.
    VkDescriptorSetLayout modal_desc_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool      modal_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       modal_descriptor_set_  = VK_NULL_HANDLE;
    VkPipelineLayout      modal_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline            modal_pipeline_        = VK_NULL_HANDLE;
    VkSampler             modal_sampler_         = VK_NULL_HANDLE;
    VkImage               modal_image_           = VK_NULL_HANDLE;
    VkDeviceMemory        modal_image_memory_    = VK_NULL_HANDLE;
    VkImageView           modal_image_view_      = VK_NULL_HANDLE;
    std::array<VkBuffer, 2>       modal_vertex_buffers_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkDeviceMemory, 2> modal_vertex_memories_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<void*, 2>          modal_vertex_mapped_{nullptr, nullptr};
    std::array<std::array<modal_vertex, 6>, 2> modal_vertices_{};
    std::array<std::uint32_t, 2>                modal_vertex_counts_{0, 0};
    data_format::boba_modal_overlay             active_modal_{};
    std::uint64_t                               modal_texture_id_{0};
    std::uint32_t                               overlay_source_width_{0};
    std::uint32_t                               overlay_source_height_{0};
    bool                                        render_boba_overlays_{false};

    // Depth pipeline resources (owned)
    VkRenderPass          depth_render_pass_     = VK_NULL_HANDLE;
    VkPipelineLayout      depth_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline            depth_pipeline_        = VK_NULL_HANDLE;
    VkDescriptorPool      depth_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout depth_desc_set_layout_ = VK_NULL_HANDLE;

    // Per-eye depth resources
    std::array<VkDescriptorSet, 2> depth_descriptor_sets_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkCommandBuffer, 2> depth_command_buffers_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkFence, 2>         depth_fences_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkFramebuffer, 2>   prev_depth_framebuffers_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkImageView, 2>     prev_depth_views_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    // Immutable sampler embedded in depth_desc_set_layout_.  Must outlive the
    // layout (and therefore the pipeline).  Created in create_depth_pipeline(),
    // destroyed in cleanup().
    VkSampler depth_immutable_sampler_ = VK_NULL_HANDLE;

    // Motion-vector pipeline resources (owned)
    /// Render pass targeting VK_FORMAT_R16G16B16A16_SFLOAT.
    VkRenderPass     mv_render_pass_     = VK_NULL_HANDLE;
    VkPipelineLayout mv_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       mv_pipeline_        = VK_NULL_HANDLE;
    /// Descriptor pool and layout are shared between color and motion-vector
    /// pipelines since both use a single combined YCbCr sampler at binding 0.
    /// Separate pools are maintained so the descriptor counts are independent.
    VkDescriptorPool      mv_descriptor_pool_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout mv_desc_set_layout_   = VK_NULL_HANDLE;
    VkSampler             mv_immutable_sampler_ = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, 2> mv_descriptor_sets_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkCommandBuffer, 2> mv_command_buffers_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkFence, 2>         mv_fences_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkFramebuffer, 2>   prev_mv_framebuffers_{VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkImageView, 2>     prev_mv_swapchain_views_{VK_NULL_HANDLE, VK_NULL_HANDLE};

    // Cached color imported images keyed by AHardwareBuffer pointer.
    std::unordered_map<AHardwareBuffer*, std::unique_ptr<imported_image>> image_cache_;

    // Cached depth imported images (separate cache — different AHardwareBuffers).
    std::unordered_map<AHardwareBuffer*, std::unique_ptr<imported_image>> depth_image_cache_;

    /// Separate cache for motion-vector buffers since they may share
    /// AHardwareBuffer pointers with depth buffers in degenerate cases.
    std::unordered_map<AHardwareBuffer*, std::unique_ptr<imported_image>> mv_image_cache_;

    // Current frame's per-eye imported image pointers (stable — heap-allocated in image_cache_).
    std::array<imported_image*, 2> current_images_{nullptr, nullptr};

    // Current frame's per-eye depth imported image pointers (into depth_image_cache_).
    std::array<imported_image*, 2> current_depth_images_{nullptr, nullptr};

    std::array<imported_image*, 2> current_mv_images_{nullptr, nullptr};

    // Crop scale factors
    float crop_scale_x_{1.0f};
    float crop_scale_y_{1.0f};

    // Extension function pointers
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID vk_get_ahb_properties_ = nullptr;

    bool initialized_            = false;
    bool has_valid_frame_        = false;
    bool pipeline_created_       = false;
    bool depth_pipeline_created_ = false;
    bool mv_pipeline_created_    = false;
    bool has_depth_frame_        = false;
    bool has_mv_frame_           = false;
#ifdef COMBINED_ENCODING
    /// True when the color decoder outputs a side-by-side combined frame.
    bool combined_encoding_ = false;
#endif
};
} // namespace ILLIXR
