#version 450

layout(set = 0, binding = 0) uniform sampler2D u_modal;

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 frag_color;

vec3 srgb_to_linear(vec3 encoded) {
    bvec3 use_linear_segment = lessThanEqual(encoded, vec3(0.04045));
    vec3 low_segment = encoded / 12.92;
    vec3 high_segment = pow((encoded + 0.055) / 1.055, vec3(2.4));
    return mix(high_segment, low_segment, use_linear_segment);
}

void main() {
    vec4 encoded = texture(u_modal, v_texcoord);
    frag_color = vec4(srgb_to_linear(encoded.rgb), encoded.a);
}
