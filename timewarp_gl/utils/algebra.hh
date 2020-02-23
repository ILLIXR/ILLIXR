/*
================================================================================================

Description	:	Vector, matrix and quaternion math.
Author		:	J.M.P. van Waveren
Date		:	12/10/2016
Language	:	C99
Format		:	Real tabs with the tab size equal to 4 spaces.
Copyright	:	Copyright (c) 2016 Oculus VR, LLC. All Rights reserved.


LICENSE
=======

Copyright (c) 2016 Oculus VR, LLC.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

DESCRIPTION
===========

All matrices are column-major.

Set one of the following defines to one before including this header file to
construct an appropriate projection matrix for a particular graphics API.

#define GRAPHICS_API_OPENGL		0
#define GRAPHICS_API_OPENGL_ES	0
#define GRAPHICS_API_VULKAN		0
#define GRAPHICS_API_D3D		0
#define GRAPHICS_API_METAL		0

*/

#if !defined( KSALGEBRA_H )
#define KSALGEBRA_H

#include <cmath>
#include <cstdbool>
#include <cassert>

class ksAlgebra {

public:

	static constexpr float MATH_PI =			3.14159265358979323846f;
	static constexpr float DEFAULT_NEAR_Z =		0.015625f;		// exact floating point representation
	static constexpr float INFINITE_FAR_Z =		0.0f;

	// 2D integer vector
	struct ksVector2i
	{
		int x;
		int y;
	};

	// 3D integer vector
	struct ksVector3i
	{
		int x;
		int y;
		int z;
	};

	// 4D integer vector
	struct ksVector4i
	{
		int x;
		int y;
		int z;
		int w;
	};

	// 2D float vector
	struct ksVector2f
	{
		float x;
		float y;
	};

	// 3D float vector
	struct ksVector3f
	{
		float x;
		float y;
		float z;
	};

	// 4D float vector
	struct ksVector4f
	{
		float x;
		float y;
		float z;
		float w;
	};

	// Quaternion
	struct ksQuatf
	{
		float x;
		float y;
		float z;
		float w;
	};

	// Column-major 2x2 matrix
	struct ksMatrix2x2f
	{
		float m[2][2];
	};

	// Column-major 2x3 matrix
	struct ksMatrix2x3f
	{
		float m[2][3];
	};

	// Column-major 2x4 matrix
	struct ksMatrix2x4f
	{
		float m[2][4];
	};

	// Column-major 3x2 matrix
	struct ksMatrix3x2f
	{
		float m[3][2];
	};

	// Column-major 3x3 matrix
	struct ksMatrix3x3f
	{
		float m[3][3];
	} ;

	// Column-major 3x4 matrix
	struct ksMatrix3x4f
	{
		float m[3][4];
	};

	// Column-major 4x2 matrix
	struct ksMatrix4x2f
	{
		float m[4][2];
	};

	// Column-major 4x3 matrix
	struct ksMatrix4x3f
	{
		float m[4][3];
	};

	// Column-major 4x4 matrix
	struct ksMatrix4x4f
	{
		float m[4][4];
	};

	static constexpr ksVector4f colorRed		= { 1.0f, 0.0f, 0.0f, 1.0f };
	static constexpr ksVector4f colorGreen		= { 0.0f, 1.0f, 0.0f, 1.0f };
	static constexpr ksVector4f colorBlue		= { 0.0f, 0.0f, 1.0f, 1.0f };
	static constexpr ksVector4f colorYellow		= { 1.0f, 1.0f, 0.0f, 1.0f };
	static constexpr ksVector4f colorPurple		= { 1.0f, 0.0f, 1.0f, 1.0f };
	static constexpr ksVector4f colorCyan		= { 0.0f, 1.0f, 1.0f, 1.0f };
	static constexpr ksVector4f colorLightGrey	= { 0.7f, 0.7f, 0.7f, 1.0f };
	static constexpr ksVector4f colorDarkGrey	= { 0.3f, 0.3f, 0.3f, 1.0f };

	static float RcpSqrt( const float x );
	static void ksVector3f_Set( ksVector3f * v, const float value );

	static void ksVector3f_Add( ksVector3f * result, const ksVector3f * a, const ksVector3f * b );

	static void ksVector3f_Sub( ksVector3f * result, const ksVector3f * a, const ksVector3f * b );

	static void ksVector3f_Min( ksVector3f * result, const ksVector3f * a, const ksVector3f * b );

	static void ksVector3f_Max( ksVector3f * result, const ksVector3f * a, const ksVector3f * b );

	static void ksVector3f_Decay( ksVector3f * result, const ksVector3f * a, const float value );

	static void ksVector3f_Lerp( ksVector3f * result, const ksVector3f * a, const ksVector3f * b, const float fraction );

	static void ksVector3f_Normalize( ksVector3f * v );

	static float ksVector3f_Length( const ksVector3f * v );

	static void ksQuatf_Lerp( ksQuatf * result, const ksQuatf * a, const ksQuatf * b, const float fraction );

	static void ksMatrix3x3f_CreateTransposeFromMatrix4x4f( ksMatrix3x3f * result, const ksMatrix4x4f * src );

	static void ksMatrix3x4f_CreateFromMatrix4x4f( ksMatrix3x4f * result, const ksMatrix4x4f * src );

	// Use left-multiplication to accumulate transformations.
	static void ksMatrix4x4f_Multiply( ksMatrix4x4f * result, const ksMatrix4x4f * a, const ksMatrix4x4f * b );

	// Creates the transpose of the given matrix.
	static void ksMatrix4x4f_Transpose( ksMatrix4x4f * result, const ksMatrix4x4f * src );

	// Returns a 3x3 minor of a 4x4 matrix.
	static float ksMatrix4x4f_Minor( const ksMatrix4x4f * matrix, int r0, int r1, int r2, int c0, int c1, int c2 );

	// Calculates the inverse of a 4x4 matrix.
	static void ksMatrix4x4f_Invert( ksMatrix4x4f * result, const ksMatrix4x4f * src );

	// Calculates the inverse of a 4x4 homogeneous matrix.
	static void ksMatrix4x4f_InvertHomogeneous( ksMatrix4x4f * result, const ksMatrix4x4f * src );

	// Creates an identity matrix.
	static void ksMatrix4x4f_CreateIdentity( ksMatrix4x4f * result );

	// Creates a translation matrix.
	static void ksMatrix4x4f_CreateTranslation( ksMatrix4x4f * result, const float x, const float y, const float z );

	// Creates a rotation matrix.
	// If -Z=forward, +Y=up, +X=right, then degreesX=pitch, degreesY=yaw, degreesZ=roll.
	static void ksMatrix4x4f_CreateRotation( ksMatrix4x4f * result, const float degreesX, const float degreesY, const float degreesZ );

	// Creates a scale matrix.
	static void ksMatrix4x4f_CreateScale( ksMatrix4x4f * result, const float x, const float y, const float z );

	// Creates a matrix from a quaternion.
	static void ksMatrix4x4f_CreateFromQuaternion( ksMatrix4x4f * result, const ksQuatf * quat );

	// Creates a combined translation(rotation(scale(object))) matrix.
	static void ksMatrix4x4f_CreateTranslationRotationScale( ksMatrix4x4f * result, const ksVector3f * translation, const ksQuatf * rotation, const ksVector3f * scale );

	// Creates a projection matrix based on the specified dimensions.
	// The projection matrix transforms -Z=forward, +Y=up, +X=right to the appropriate clip space for the graphics API.
	// The far plane is placed at infinity if farZ <= nearZ.
	// An infinite projection matrix is preferred for rasterization because, except for
	// things *right* up against the near plane, it always provides better precision:
	//		"Tightening the Precision of Perspective Rendering"
	//		Paul Upchurch, Mathieu Desbrun
	//		Journal of Graphics Tools, Volume 16, Issue 1, 2012
	static void ksMatrix4x4f_CreateProjection( ksMatrix4x4f * result, const float tanAngleLeft, const float tanAngleRight,
												const float tanAngleUp, float const tanAngleDown, const float nearZ, const float farZ );

	// Creates a projection matrix based on the specified FOV.
	static void ksMatrix4x4f_CreateProjectionFov( ksMatrix4x4f * result, const float fovDegreesLeft, const float fovDegreesRight,
													const float fovDegreesUp, const float fovDegreesDown, const float nearZ, const float farZ );

	// Creates a matrix that transforms the -1 to 1 cube to cover the given 'mins' and 'maxs' transformed with the given 'matrix'.
	static void ksMatrix4x4f_CreateOffsetScaleForBounds( ksMatrix4x4f * result, const ksMatrix4x4f * matrix, const ksVector3f * mins, const ksVector3f * maxs );

	// Returns true if the given matrix is affine.
	static bool ksMatrix4x4f_IsAffine( const ksMatrix4x4f * matrix, const float epsilon );

	// Returns true if the given matrix is orthogonal.
	static bool ksMatrix4x4f_IsOrthogonal( const ksMatrix4x4f * matrix, const float epsilon );

	// Returns true if the given matrix is orthonormal.
	static bool ksMatrix4x4f_IsOrthonormal( const ksMatrix4x4f * matrix, const float epsilon );

	// Returns true if the given matrix is homogeneous.
	static bool ksMatrix4x4f_IsHomogeneous( const ksMatrix4x4f * matrix, const float epsilon );

	// Get the translation from a combined translation(rotation(scale(object))) matrix.
	static void ksMatrix4x4f_GetTranslation( ksVector3f * result, const ksMatrix4x4f * src );

	// Get the rotation from a combined translation(rotation(scale(object))) matrix.
	static void ksMatrix4x4f_GetRotation( ksQuatf * result, const ksMatrix4x4f * src );

	// Get the scale from a combined translation(rotation(scale(object))) matrix.
	static void ksMatrix4x4f_GetScale( ksVector3f * result, const ksMatrix4x4f * src );

	// Transforms a 3D vector.
	static void ksMatrix4x4f_TransformVector3f( ksVector3f * result, const ksMatrix4x4f * m, const ksVector3f * v );

	// Transforms a 4D vector.
	static void ksMatrix4x4f_TransformVector4f( ksVector4f * result, const ksMatrix4x4f * m, const ksVector4f * v );

	// Transforms the 'mins' and 'maxs' bounds with the given 'matrix'.
	static void ksMatrix4x4f_TransformBounds( ksVector3f * resultMins, ksVector3f * resultMaxs, const ksMatrix4x4f * matrix,
												const ksVector3f * mins, const ksVector3f * maxs );

	// Returns true if the 'mins' and 'maxs' bounds is completely off to one side of the projection matrix.
	static bool ksMatrix4x4f_CullBounds( const ksMatrix4x4f * mvp, const ksVector3f * mins, const ksVector3f * maxs );
		
};

#endif // !KSALGEBRA_H
