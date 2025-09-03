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

#version 450 

layout (set = 0, binding = 0) uniform sampler2D image_texture;
layout (set = 0, binding = 1) uniform sampler2D depth_texture;

float u_debugOpacity = 0.0f;

layout (location = 0) in vec4 worldspace;
layout (location = 1) in vec2 warpUv;

layout (location = 0) out vec4 outColor;

void main()
{
    vec2 uv = clamp(warpUv, vec2(0.0, 0.0), vec2(1.0, 1.0));
    outColor = texture(image_texture, uv);

    // float depth = gl_FragCoord.z;
    // outColor = vec4(vec3(depth), 1.0f);

    // Worldspace parameterization grid overlay.
    // For debug + visualization only
    // vec3 worldspace_adjusted = vec3(1,1,1) * 0.02 + worldspace.xyz;
    // vec3 debugGrid = mod(worldspace_adjusted + 0.005*vec3(1,1,1), 0.05) - mod(worldspace_adjusted, 0.05);
    // outColor.rgb -= debugGrid * 2.0 * u_debugOpacity;
}
