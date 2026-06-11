#version 450

// See color.frag for why GL_EXT_samplerless_texture_functions must not be declared.
layout(set = 0, binding = 0) uniform sampler2D u_depth_texture;

layout(location = 0) in vec2 v_texcoord;

// ── Encoding chain ────────────────────────────────────────────────────────────
// Server stores VK_FORMAT_D16_UNORM clip-space (NDC) depth in [0, 1].
// depth16_to_rg compute shader packs it into R8G8_UNORM:
//   R = (d16 >> 8) & 0xFF   ← high byte
//   G = (d16     ) & 0xFF   ← low byte (written to both U and V NV12 planes)
//
// NVENC encodes as HEVC Main 8-bit (NV12, no explicit color metadata).
// Snapdragon XR2 driver reports VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY
// for this stream — passthrough: R←Y (high byte), G←U (low byte), no matrix.
//
// Reconstruction:
//   depth_ndc ≈ (high * 256 + low) / 65535
//             = encoded.r * 255 * 256 / 65535 + encoded.g * 255 / 65535
//
// This is the NDC clip-space depth expected by both
// XrCompositionLayerDepthInfoKHR and XrCompositionLayerSpaceWarpInfoFB.
// No linear↔NDC conversion is needed: the server stores NDC, not linear depth.
// ──────────────────────────────────────────────────────────────────────────────

void main() {
    vec4 encoded = texture(u_depth_texture, v_texcoord);

    // Recover integer high and low bytes, then reconstruct the 16-bit value.
    // Scaling by 255/65535 ≈ 1/257 corrects the slight bias in the simpler
    // "r + g/256" formula (which divides by 65280 instead of 65535).
    float high = encoded.r * 255.0;
    float low  = encoded.g * 255.0;
    float depth = (high * 256.0 + low) / 65535.0;

    gl_FragDepth = clamp(depth, 0.0, 1.0);
}
