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

// uniform highp mat4x4 u_renderInverseP;
// uniform highp mat4x4 u_renderInverseV;
// uniform highp mat4x4 u_warpVP;

// uniform mediump float bleedRadius;
// uniform mediump float edgeTolerance;

// layout(location = 0) in vec3 in_position;
// layout(location = 1) in vec2 in_uv;

// layout(binding = 1) uniform highp sampler2D Texture;
// layout(binding = 2) uniform highp sampler2D _Depth;
// out mediump vec4 worldspace;
// out mediump vec2 warpUv;
// out gl_PerVertex { vec4 gl_Position; };

// void main( void )
// {
// 	float z = textureLod(_Depth, in_uv, 0.0).x * 2.0 - 1.0;

// 	float outlier = min(              											
// 					  min(														
// 							textureLod(_Depth, in_uv - vec2(bleedRadius,0), 0).x, 
// 							textureLod(_Depth, in_uv + vec2(bleedRadius,0), 0).x  
// 					  ),														
// 					  min(
// 							textureLod(_Depth, in_uv - vec2(0,bleedRadius), 0).x, 
// 							textureLod(_Depth, in_uv + vec2(0,bleedRadius), 0).x  
// 					  )
// 					);

// 	float diags = min(textureLod(_Depth, in_uv + sqrt(2) * vec2(bleedRadius,bleedRadius), 0).x,
// 				textureLod(_Depth, in_uv - sqrt(2) * vec2(bleedRadius,bleedRadius), 0).x);

// 	outlier = min(diags, outlier);

// 	outlier = outlier * 2.0 - 1.0;
// 	if(z - outlier > edgeTolerance){
// 		z = outlier;
// 	}
// 	z = min(0.99, z);

// 	vec4 clipSpacePosition = vec4(in_uv * 2.0 - 1.0, z, 1.0);
// 	vec4 frag_viewspace = u_renderInverseP * clipSpacePosition;
// 	vec4 frag_worldspace = (u_renderInverseV * frag_viewspace);
// 	vec4 result = u_warpVP * frag_worldspace;

// 	result /= abs(result.w);
// 	gl_Position = result;
// 	worldspace = frag_worldspace;
// 	warpUv = in_uv;
// }

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