#version 450

// Motion-vector reconstruction shader for XrCompositionLayerSpaceWarpInfoFB.
//
// ── Encoding chain (server side) ─────────────────────────────────────────────
// Source:  VK_FORMAT_R16G16B16A16_SFLOAT from Unity's motion vector pass.
//          R = Vx, G = Vy  (NDC clip-space displacement per frame)
//          MV_MAX_VEL = 2.0  (max NDC displacement, covers extreme head motion)
//
// CUDA P010 kernel (launch_rgba16f_to_p010_scaled):
//   quantised = round((v / MV_MAX_VEL * 0.5 + 0.5) * 1023)   → [0, 1023]
//   uint16_t  = quantised << 6                                 (P010 MSB packing)
//   Y  plane  ← quantised Vx  (full-resolution)
//   U  plane  ← quantised Vy  (2×2 subsampled)
//   V  plane  ← quantised Vz  (2×2 subsampled, unused by spacewarp)
//
// NVENC → HEVC Main 10 → MediaCodec → AHardwareBuffer (P010)
//
// ── Vulkan import (client side) ──────────────────────────────────────────────
// import_mv_hardware_buffer() forces VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY.
// This is a passthrough — NO BT.601 color matrix is applied.
// The sampler returns:
//   R ← Y_norm  = quantised_y / 1023  = Vx normalised to [0, 1]
//   G ← U_norm  = quantised_u / 1023  = Vy normalised to [0, 1]  (bilinear upsampled from half-res)
//   B ← V_norm  = quantised_v / 1023  = Vz normalised to [0, 1]
//
// ── Dequantisation ───────────────────────────────────────────────────────────
//   Vx = (R − 0.5) * 2.0 * MV_MAX_VEL
//   Vy = (G − 0.5) * 2.0 * MV_MAX_VEL
//
// ── Output ───────────────────────────────────────────────────────────────────
// The R16G16B16A16_SFLOAT motion-vector swapchain for spacewarp.
// XrCompositionLayerSpaceWarpInfoFB uses the RG components as NDC clip-space
// displacement vectors. Vz (depth motion) is not used by Quest 3 spacewarp.
//
// ── Compilation ──────────────────────────────────────────────────────────────
//   glslc --target-env=vulkan1.1 motion_vec.frag -o motion_vec_frag.spv
//   xxd -i motion_vec_frag.spv > motion_vec_frag_spv.h

// Combined image sampler — must use immutable YCbCr sampler (even with RGB_IDENTITY).
// See color.frag for why GL_EXT_samplerless_texture_functions must NOT be declared.
layout(set = 0, binding = 0) uniform sampler2D u_mv_texture;

// Push constants (same layout as color pipeline: crop scale only).
// Crop removes H.265 alignment padding from the decoded buffer.
layout(push_constant) uniform push_constants_t {
    float crop_scale_x;
    float crop_scale_y;
} push_constants;

layout(location = 0) in  vec2 v_texcoord;
layout(location = 0) out vec4 frag_color;

// Must match the server-side MV_MAX_VEL in color_convert.cu.
const float MV_MAX_VEL = 2.0;

void main() {
    // Apply crop to discard H.265 alignment padding.
    vec2 uv = v_texcoord * vec2(push_constants.crop_scale_x,
                                push_constants.crop_scale_y);

    // With RGB_IDENTITY the sampler returns (Y_norm, U_norm, V_norm) directly.
    // No BT.601 matrix is applied.  The values are the raw P010 10-bit samples
    // normalised to [0, 1] by the Vulkan driver.
    vec4 s = texture(u_mv_texture, uv);

    // Dequantise: map [0, 1] back to [-MV_MAX_VEL, +MV_MAX_VEL] NDC.
    float vx = (s.r - 0.5) * 2.0 * MV_MAX_VEL;
    float vy = (s.g - 0.5) * 2.0 * MV_MAX_VEL;

    // OpenXR spacewarp reads RG as the per-pixel screen-space motion vector.
    // BA are unused; set to sentinel values for easier debugging.
    frag_color = vec4(vx, vy, 0.0, 1.0);
}
