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
    mat4 u_renderInverseP[2];
    mat4 u_renderInverseV[2];
    mat4 u_warpVP[2];
} warp_matrices;

layout (push_constant) uniform Eye {
    uint index;
} eye;

// Constant for now
float bleedRadius = 0.05f;
float edgeTolerance = 0.001f;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 worldspace;
layout (location = 1) out vec2 warpUv;

// Define this macro if the OpenXR application is using reverse depth
#define REVERSE_Z

void main( void )
{
	float z = textureLod(depth_texture, in_uv, 0.0).x;

#ifdef REVERSE_Z
	float outlier = max(              											
					  max(														
							textureLod(depth_texture, in_uv - vec2(bleedRadius,0), 0).x, 
							textureLod(depth_texture, in_uv + vec2(bleedRadius,0), 0).x  
					  ),														
					  max(
							textureLod(depth_texture, in_uv - vec2(0,bleedRadius), 0).x, 
							textureLod(depth_texture, in_uv + vec2(0,bleedRadius), 0).x  
					  )
					);

	float diags = max(textureLod(depth_texture, in_uv + vec2(bleedRadius, bleedRadius) / sqrt(2), 0).x,
					  textureLod(depth_texture, in_uv - vec2(bleedRadius, bleedRadius) / sqrt(2), 0).x);

	outlier = max(diags, outlier);
	if(outlier - z > edgeTolerance){
		z = outlier;
	}
	z = (z > 0.04045) ? pow((z + 0.055) / 1.055, 2.4) : z / 12.0;
	z = max(0.01, z);
#else
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
	if(z - outlier > edgeTolerance){
		z = outlier;
	}
	z = min(0.99, z);
#endif

	vec4 clipSpacePosition = vec4(in_uv.x * 2.0 - 1.0, 1.0 - in_uv.y * 2.0, z, 1.0);
	vec4 frag_viewspace = warp_matrices.u_renderInverseP[eye.index] * clipSpacePosition;
	frag_viewspace /= frag_viewspace.w;
	vec4 frag_worldspace = (warp_matrices.u_renderInverseV[eye.index] * frag_viewspace);
	 vec4 result = warp_matrices.u_warpVP[eye.index] * frag_worldspace;
	 result /= abs(result.w);

	// Uncomment the line below to disable warping.
    // result = vec4(in_uv.x * 2.0 - 1.0, 1.0 - in_uv.y * 2.0, 0.5, 1.0);

	gl_Position = result;

	worldspace = frag_worldspace;
	warpUv = in_uv;
}
