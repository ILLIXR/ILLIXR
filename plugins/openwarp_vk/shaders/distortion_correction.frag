#version 450

// Enabled by the MONADO_REQUIRED plugin variant for native UNORM presentation.
layout(constant_id = 0) const bool encode_srgb = false;

vec3 linear_to_srgb(vec3 color) {
    color = clamp(color, 0.0, 1.0);
    return mix(1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
               12.92 * color, lessThanEqual(color, vec3(0.0031308)));
}

layout(binding = 0) uniform sampler2D Texture;
layout(location = 0) in mediump vec2 fragmentUv0;
layout(location = 1) in mediump vec2 fragmentUv1;
layout(location = 2) in mediump vec2 fragmentUv2;
layout(location = 0) out lowp vec4 outColor;

void main() {
    outColor.r = texture( Texture, fragmentUv0 ).r;
    outColor.g = texture( Texture, fragmentUv1 ).g;
    outColor.b = texture( Texture, fragmentUv2 ).b;
    if (encode_srgb) {
        outColor.rgb = linear_to_srgb(outColor.rgb);
    }
    outColor.a = 1.0;
}
