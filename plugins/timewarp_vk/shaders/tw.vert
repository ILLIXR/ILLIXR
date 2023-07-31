#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 TimeWarpStartTransform;
    mat4 TimeWarpEndTransform;
} ubo;

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexUv0;
layout(location = 2) in vec2 vertexUv1;
layout(location = 3) in vec2 vertexUv2;

layout(location = 0) out mediump vec2 fragmentUv0;
layout(location = 1) out mediump vec2 fragmentUv1;
layout(location = 2) out mediump vec2 fragmentUv2;

void main() {
    gl_Position = vec4( vertexPosition, 1.0 );
    float displayFraction = vertexPosition.x * 0.5 + 0.5;
    vec3 startUv0 = (ubo.TimeWarpStartTransform * vec4( vertexUv0, -1, 1 )).xyz;
    vec3 startUv1 = (ubo.TimeWarpStartTransform * vec4( vertexUv1, -1, 1 )).xyz;
    vec3 startUv2 = (ubo.TimeWarpStartTransform * vec4( vertexUv2, -1, 1 )).xyz;

    vec3 endUv0 = (ubo.TimeWarpEndTransform * vec4( vertexUv0, -1, 1 )).xyz;
    vec3 endUv1 = (ubo.TimeWarpEndTransform * vec4( vertexUv1, -1, 1 )).xyz;
    vec3 endUv2 = (ubo.TimeWarpEndTransform * vec4( vertexUv2, -1, 1 )).xyz;

    vec3 curUv0 = mix( startUv0, endUv0, displayFraction );
    vec3 curUv1 = mix( startUv1, endUv1, displayFraction );
    vec3 curUv2 = mix( startUv2, endUv2, displayFraction );

    fragmentUv0 = curUv0.xy * ( 1.0 / max( curUv0.z, 0.00001 ) );
    fragmentUv1 = curUv1.xy * ( 1.0 / max( curUv1.z, 0.00001 ) );
    fragmentUv2 = curUv2.xy * ( 1.0 / max( curUv2.z, 0.00001 ) );
}