#version 450

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexUv0;
layout(location = 2) in vec2 vertexUv1;
layout(location = 3) in vec2 vertexUv2;

layout(location = 0) out mediump vec2 fragmentUv0;
layout(location = 1) out mediump vec2 fragmentUv1;
layout(location = 2) out mediump vec2 fragmentUv2;

void main() {
    gl_Position = vec4( vertexPosition, 1.0 );

    fragmentUv0 = vertexUv0;
    fragmentUv1 = vertexUv1;
    fragmentUv2 = vertexUv2;
}