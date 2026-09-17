#version 450

// Combined image sampler with implicit YCbCr conversion.
// The descriptor set layout must declare this as an immutable sampler.
// NOTE: GL_EXT_samplerless_texture_functions must NOT be declared here.
// Some glslang versions will emit SPIR-V that separates this combined
// image sampler into a (texture2D + sampler) pair when that extension is
// active.  The Adreno driver (and the Vulkan spec) require that an
// immutable YCbCr sampler is always accessed through a true combined
// image sampler — a separated path causes VK_ERROR_INITIALIZATION_FAILED
// from vkCreateGraphicsPipelines.
layout(set = 0, binding = 0) uniform sampler2D u_ycbcr_texture;

layout(location = 0) in  vec2 v_texcoord;
layout(location = 0) out vec4 frag_color;

// NOTE(ILLIXR): u_ycbcr_texture's implicit VkSamplerYcbcrConversion only
// performs the YUV->RGB colour-matrix conversion -- it does not decode any
// gamma/transfer function. The sampled RGB here is therefore already
// gamma-encoded (it was gamma-encoded on the server before NVENC ever saw
// it). This render pass's color attachment is a *_SRGB format (see
// create_render_pass() in stereo_renderer.cpp), and the OpenXR runtime
// does not offer a UNORM alternative for this swapchain, so every store to
// it is automatically re-encoded from linear to gamma by the hardware.
// Writing the sampled value straight through, as before, encoded it a
// second time -- that's what was producing the over-bright image.
// Decoding back to linear here first cancels that automatic encode, so
// what actually lands in the swapchain matches the originally encoded
// value: a single correct gamma pass, not two.
vec3 srgb_decode(vec3 c) {
    bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
    vec3 lo      = c / 12.92;
    vec3 hi      = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(hi, lo, cutoff);
}

void main() {
    vec4 sampled = texture(u_ycbcr_texture, v_texcoord);
    frag_color   = vec4(srgb_decode(sampled.rgb), sampled.a);
}
