#version 450

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 frag_color;

vec3 srgb_to_linear(vec3 encoded) {
    bvec3 use_linear_segment = lessThanEqual(encoded, vec3(0.04045));
    vec3 low_segment = encoded / 12.92;
    vec3 high_segment = pow((encoded + 0.055) / 1.055, vec3(2.4));
    return mix(high_segment, low_segment, use_linear_segment);
}

void main() {
    frag_color = vec4(srgb_to_linear(v_color.rgb), v_color.a);
}
