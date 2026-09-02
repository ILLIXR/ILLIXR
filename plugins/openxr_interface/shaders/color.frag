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

vec3 srgb_to_linear(vec3 encoded) {
    bvec3 use_linear_segment = lessThanEqual(encoded, vec3(0.04045));
    vec3 low_segment = encoded / 12.92;
    vec3 high_segment = pow((encoded + 0.055) / 1.055, vec3(2.4));
    return mix(high_segment, low_segment, use_linear_segment);
}

void main() {
    // MediaCodec's YCbCr sampler returns display-encoded RGB. Render-pass
    // color attachments expect linear shader output (and sRGB attachments
    // apply their own transfer on store), so linearize exactly once here.
    vec4 encoded = texture(u_ycbcr_texture, v_texcoord);
    frag_color = vec4(srgb_to_linear(encoded.rgb), encoded.a);
}
