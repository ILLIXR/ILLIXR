#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 modelview;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec2 uv;

void main() {
    gl_Position = ubo.proj * ubo.modelview * vec4(inPosition, 1.0);
    uv = inUV;
}