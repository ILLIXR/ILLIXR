#version 450

layout(binding = 1) uniform sampler samp;
layout(binding = 2) uniform texture2D tex[4];
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outDepth;

layout(push_constant) uniform PER_OBJECT { int tex_ind; } pc;

void main() {
    // Sample the texture at the interpolated coordinate
    outColor = texture(sampler2D(tex[pc.tex_ind], samp), uv);
    outDepth = vec4(vec3(gl_FragCoord.z), 1.0f);
}