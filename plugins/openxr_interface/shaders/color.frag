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

void main() {
    frag_color = texture(u_ycbcr_texture, v_texcoord);
}
