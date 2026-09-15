#version 450

layout(location = 0) in vec2 a_position_pixels;
layout(location = 1) in vec4 a_color;

layout(location = 0) out vec4 v_color;

layout(push_constant) uniform PushConstants {
    vec2 source_size;
} pc;

void main() {
    vec2 safe_size = max(pc.source_size, vec2(1.0));
    // The Quest renderer uses a positive-height Vulkan viewport, where NDC
    // y=-1 maps to the top of the framebuffer. Boba's pixel coordinates use a
    // top-left origin, so y must increase from -1 to +1 here. The OpenGL
    // viewer uses the opposite NDC convention and therefore cannot be copied
    // verbatim.
    vec2 ndc = vec2(
        (a_position_pixels.x / safe_size.x) * 2.0 - 1.0,
        (a_position_pixels.y / safe_size.y) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = a_color;
}
