#version 450

layout(binding = 1) uniform sampler2D Texture;
layout(location = 0) in mediump vec2 fragmentUv0;
layout(location = 1) in mediump vec2 fragmentUv1;
layout(location = 2) in mediump vec2 fragmentUv2;
layout(location = 0) out lowp vec4 outColor;

void main() {
    outColor.r = texture( Texture, fragmentUv0 ).r;
    outColor.g = texture( Texture, fragmentUv1 ).g;
    outColor.b = texture( Texture, fragmentUv2 ).b;
    outColor.a = 1.0;
}