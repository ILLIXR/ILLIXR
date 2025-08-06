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
float bleedRadius = 0.002f;
float edgeTolerance = 0.01f;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 worldspace;
layout (location = 1) out vec2 warpUv;

// Define this macro if the OpenXR application is using reverse depth
// TODO: pass this in as an argument!
#define REVERSE_Z


// Convert linearized depth back to clip space for coordinate transformation
float linearDepthToClipSpace(float linear_depth) {
    float near = 0.05;
    float far = 4000.0;
    float offset = 0.05;
    
    // Convert linear depth to view space depth
    float z_view = -linear_depth;  // Linear depth is positive distance, view space is negative
    
    // Apply the Godot projection transformation
    float A = -(far + offset) / (far - near);
    float B = -(far * (near + offset)) / (far - near);
    float z_ndc = (A * z_view + B) / -z_view;
    
    // Convert to [0,1] range (Godot's remapping)
    return z_ndc * -0.5 + 0.5;
}

// Get linearized depth for bleeding calculations
float linearizeDepth(float depth_nl) {
    float near = 0.05;    
    float far = 4000.0;  
    float offset = near;  

    // Reverse Godot's depth transformation
    float depth_ndc = depth_nl * -2.0 + 1.0;  // [1,0] -> [-1,1]
    
    // Reverse the projection: z_ndc = A*z_view + B
    // where A = -(far + offset)/(far - near), B = -(far * (near + offset))/(far - near)
    float A = -(far + offset) / (far - near);
    float B = -(far * (near + offset)) / (far - near);
    
    // Solve for z_view: z_view = (z_ndc - B) / A
    float z_view = -B / (A + depth_ndc);

    // Linear depth is the positive distance from camera
    return -z_view;
}

float getDepth(vec2 uv){
	float depth = textureLod(depth_texture, uv, 0.0).x; // nonlinear depth
	return linearizeDepth(depth);
}

float getRawDepth(vec2 uv) {
	return textureLod(depth_texture, uv, 0.0).x;
}

void main( void )
{
	float z_linear = getDepth(in_uv);
	float inv = 1.0 / sqrt(2.0);

	float outlier = min(
					  min(
					  		getDepth(in_uv - vec2(bleedRadius, 0)),
							getDepth(in_uv + vec2(bleedRadius, 0))
					  ),
					  min(
							getDepth(in_uv - vec2(0, bleedRadius)),
							getDepth(in_uv + vec2(0, bleedRadius))
					  )
					);

	float diags = min(
					min(
						getDepth(in_uv + inv * vec2(bleedRadius, bleedRadius)),
					  	getDepth(in_uv + inv * vec2(-bleedRadius, -bleedRadius))
					),
					min(
						getDepth(in_uv + inv * vec2(-bleedRadius, bleedRadius)),
						getDepth(in_uv + inv * vec2(bleedRadius, -bleedRadius))
					)
				  );

	outlier = min(diags, outlier);
	if(z_linear - outlier > edgeTolerance){
		z_linear = outlier;
	}
	
	// z_linear = clamp(z_linear, 0.05, 3999.9);
	float z_clip = linearDepthToClipSpace(z_linear);

 	vec4 clipSpacePosition = vec4(in_uv.x * 2.0 - 1.0, 1.0 - in_uv.y * 2.0, z_clip, 1.0);
	vec4 frag_viewspace = warp_matrices.u_renderInverseP[eye.index] * clipSpacePosition;
	frag_viewspace /= frag_viewspace.w;
    vec4 frag_worldspace = (warp_matrices.u_renderInverseV[eye.index] * frag_viewspace);

	vec4 result = warp_matrices.u_warpVP[eye.index] * frag_worldspace;
	result /= abs(result.w);
    // in case z becomes negative, clamp it to 0.001
    result.z = max(0.001, result.z);

	gl_Position = result;

	worldspace = frag_worldspace;
	warpUv = in_uv;
}