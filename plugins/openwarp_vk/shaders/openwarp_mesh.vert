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

layout (set = 0, binding = 2) uniform Matrices {
    mat4 u_renderInverseP;
    mat4 u_renderInverseV;
    mat4 u_warpVP;
} warp_matrices;

// Constant for now
float bleedRadius = 0.005f;
float edgeTolerance = 0.0001f;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 worldspace;
layout (location = 1) out vec2 warpUv;

void main( void )
{
	float z = (textureLod(depth_texture, in_uv, 0.0).x * 2.0 - 1.0);

	float outlier = min(              											
					  min(														
							textureLod(depth_texture, in_uv - vec2(bleedRadius,0), 0).x, 
							textureLod(depth_texture, in_uv + vec2(bleedRadius,0), 0).x  
					  ),														
					  min(
							textureLod(depth_texture, in_uv - vec2(0,bleedRadius), 0).x, 
							textureLod(depth_texture, in_uv + vec2(0,bleedRadius), 0).x  
					  )
					);

	float diags = min(textureLod(depth_texture, in_uv + sqrt(2) * vec2(bleedRadius, bleedRadius), 0).x,
					  textureLod(depth_texture, in_uv - sqrt(2) * vec2(bleedRadius, bleedRadius), 0).x);

	outlier = min(diags, outlier);

	outlier = outlier * 2.0 - 1.0;
	if(z - outlier > edgeTolerance){
		z = outlier;
	}
	// to-do: need a parameter to control reverse depth here
	// z = min(0.99, z);
	z = max(0.01, z);

	vec4 clipSpacePosition = vec4(in_uv * 2.0 - 1.0, z, 1.0);
	vec4 frag_viewspace = warp_matrices.u_renderInverseP * clipSpacePosition;
	vec4 frag_worldspace = (warp_matrices.u_renderInverseV * frag_viewspace);
	vec4 result = warp_matrices.u_warpVP * frag_worldspace;

	result /= abs(result.w);
	gl_Position = result;
	// gl_Position = clipSpacePosition;
	worldspace = frag_worldspace;
	warpUv = in_uv;
}