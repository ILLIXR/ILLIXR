#version 450

// Full-screen triangle vertex shader.
//
// IMPORTANT — Adreno compatibility notes:
//
// 1. All integer arithmetic must use a single consistent type.
//    gl_VertexIndex is 'int'; mixing it with unsigned literals (1u, 2u) in
//    bitwise operations produces SPIR-V with mixed-signedness OpShiftLeft /
//    OpBitwiseAnd instructions that some Adreno driver versions reject at
//    native-code compilation time ("Shader compilation failed for shaderType: 0").
//    All arithmetic here is kept as plain 'int'.
//
// 2. This shader MUST be compiled targeting SPIR-V 1.4 or earlier:
//      glslc --target-env=vulkan1.1 color.vert -o color_vert.spv
//    Targeting vulkan1.2/1.3 (SPIR-V 1.5/1.6) also causes driver rejection on
//    Quest 3 (Adreno 740) with older firmware.
layout(location = 0) out vec2 v_texcoord;
layout(push_constant) uniform PushConstants {
    float crop_scale_x;
    float crop_scale_y;
    float u_offset;
} pc;

void main() {
    // Full-screen triangle trick: generate a triangle large enough that the
    // rasterizer clips it to fill the entire NDC square [-1,1]x[-1,1].
    //
    // Vertices must extend *beyond* NDC space: (-1,-1), (3,-1), (-1,3).
    // The bit ops must produce fx: 0, 4, 0 and fy: 0, 0, 4:
    //
    //   pos_x = (vi & 1) << 2  ->  0, 4, 0  (NOT (vi<<1)&2 which gives 0,2,0)
    //   pos_y = (vi & 2) << 1  ->  0, 0, 4  (NOT vi&2       which gives 0,0,2)
    //
    // The old (vi<<1)&2 / vi&2 pattern only reached NDC corners (1,-1)/(-1,1),
    // producing a triangle that covered only the lower-left half of the screen.
    //
    // All operations are kept in plain 'int' to avoid mixed-signedness SPIR-V.
    int vi    = gl_VertexIndex;
    int pos_x = (vi & 1) << 2;  // 0, 4, 0
    int pos_y = (vi & 2) << 1;  // 0, 0, 4

    float fx = float(pos_x);  // 0.0, 4.0, 0.0
    float fy = float(pos_y);  // 0.0, 0.0, 4.0

    gl_Position = vec4(fx - 1.0, fy - 1.0, 0.0, 1.0);

    // UV: map NDC position to [0,1] texture coordinates.
    // In Vulkan NDC, Y=-1 is the TOP of the screen and Y=+1 is the bottom,
    // which already matches texture V=0 (top) -> V=1 (bottom) directly.
    // No Y-flip is needed — applying one (as in OpenGL) would invert the image.
    float u = fx * 0.5;
    float v = fy * 0.5;
    v_texcoord = vec2(pc.u_offset + u * pc.crop_scale_x, v * pc.crop_scale_y);
}
