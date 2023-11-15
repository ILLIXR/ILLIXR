/*
Copyright (c) 2020 Finn Sinclair.  All rights reserved.

Developed by: Finn Sinclair
              University of Illinois at Urbana-Champaign
              finnsinclair.com

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal with
the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to
do so, subject to the following conditions:
* Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimers.
* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimers in the documentation
  and/or other materials provided with the distribution.
* Neither the names of Finn Sinclair, University of Illinois at Urbana-Champaign,
  nor the names of its contributors may be used to endorse or promote products
  derived from this Software without specific prior written permission.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
SOFTWARE.
*/

// #version 450 

// layout(binding = 1) uniform highp sampler2D Texture;
// layout(binding = 2) uniform highp sampler2D _Depth;

// uniform lowp float u_debugOpacity;

// in mediump vec4 worldspace;
// in mediump vec2 warpUv;
// out mediump vec4 outColor;
// void main()
// {
//     outColor.rgba = texture(Texture, warpUv);

//     // Worldspace parameterization grid overlay.
//     // For debug + visualization only
//     vec3 worldspace_adjusted = vec3(1,1,1) * 0.02 + worldspace.xyz;
//     vec3 debugGrid = mod(worldspace_adjusted + 0.005*vec3(1,1,1), 0.05) - mod(worldspace_adjusted, 0.05);
//     outColor.rgb -= debugGrid * 2.0 * u_debugOpacity;
// }

#version 450

layout(binding = 1) uniform sampler2D Texture;
layout(binding = 2) uniform sampler2D Depth;
layout(location = 0) in mediump vec2 fragmentUv0;
layout(location = 1) in mediump vec2 fragmentUv1;
layout(location = 2) in mediump vec2 fragmentUv2;
layout(location = 0) out lowp vec4 outColor;

void main() {
    outColor.r = texture( Texture, fragmentUv0 ).r;
    outColor.g = texture( Texture, fragmentUv0 ).g;
    outColor.b = texture( Texture, fragmentUv0 ).b;
    outColor.a = 1.0;
}