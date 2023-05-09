#version 450

layout(binding = 1) uniform sampler samp;
layout(binding = 2) uniform texture2D tex[4];
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PER_OBJECT { int tex_ind; } pc;

void main() {
    outColor = texture(sampler2D(tex[pc.tex_ind], samp), uv);
}