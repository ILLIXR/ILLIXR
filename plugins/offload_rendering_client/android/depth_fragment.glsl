#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision highp float;

uniform samplerExternalOES u_depth_texture;
in vec2 v_texcoord;

void main() {
    // Sample NV12 texture with RG depth bytes
    vec4 sample = texture(u_depth_texture, v_texcoord);

    // Unpack: R=high byte, G=low byte
    uint r = uint(sample.r * 255.0 + 0.5);
    uint g = uint(sample.g * 255.0 + 0.5);
    uint depth_16bit = (r << 8u) | g;

    // Write to depth buffer
    gl_FragDepth = float(depth_16bit) / 65535.0;
}