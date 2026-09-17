#version 450

layout(location = 0) in vec2 a_position_pixels;
layout(location = 1) in vec2 a_texcoord;

layout(location = 0) out vec2 v_texcoord;

layout(push_constant) uniform PushConstants {
    vec2 source_size;
} pc;

void main() {
    vec2 safe_size = max(pc.source_size, vec2(1.0));
    // Positive-height Vulkan viewports map NDC y=-1 to the top. Boba's quad
    // coordinates are top-left-origin pixels, so y increases toward +1.
    vec2 ndc = vec2(
        (a_position_pixels.x / safe_size.x) * 2.0 - 1.0,
        (a_position_pixels.y / safe_size.y) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_texcoord = a_texcoord;
}
