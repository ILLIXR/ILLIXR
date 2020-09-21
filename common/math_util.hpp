#pragma once

namespace ILLIXR {
	namespace math_util {

		// Calculates a projection matrix with given properties. Implementation
		// borrowed from J.M.P. Van Waveren's ksAlgebra library, as found in
		// https://github.com/KhronosGroup/Vulkan-Samples-Deprecated/blob/master/samples/apps/atw/atw_opengl.c
		void projection_fov(Eigen::Matrix4f* result,
								   const float fovLeft, const float fovRight,
								   const float fovUp, const float fovDown,
								   const float nearZ, const float farZ);

		// Calculates a projection matrix with given properties. Implementation
		// borrowed from J.M.P. Van Waveren's ksAlgebra library, as found in
		// https://github.com/KhronosGroup/Vulkan-Samples-Deprecated/blob/master/samples/apps/atw/atw_opengl.c
		void projection( Eigen::Matrix4f* result,
								const float tanAngleLeft, const float tanAngleRight,
                                const float tanAngleUp, float const tanAngleDown,
								const float nearZ, const float farZ );

		void projection_fov(Eigen::Matrix4f* result,
								   const float fovLeft, const float fovRight,
								   const float fovUp, const float fovDown,
								   const float nearZ, const float farZ) {

			const float tanLeft = - tanf( fovLeft * ( M_PI / 180.0f ) );
			const float tanRight = tanf( fovRight * ( M_PI / 180.0f ) );

			const float tanDown = - tanf( fovDown * ( M_PI / 180.0f ) );
			const float tanUp = tanf( fovUp * ( M_PI / 180.0f ) );
			
			projection(result, tanLeft, tanRight, tanUp, tanDown, nearZ, farZ );
		}

		void projection( Eigen::Matrix4f* result,
								const float tanAngleLeft, const float tanAngleRight,
                                const float tanAngleUp, float const tanAngleDown,
								const float nearZ, const float farZ ) {
			const float tanAngleWidth = tanAngleRight - tanAngleLeft;
			// Set to tanAngleUp - tanAngleDown for a clip space with positive Y up (OpenGL / D3D / Metal).
			const float tanAngleHeight = tanAngleUp - tanAngleDown;
			// Set to nearZ for a [-1,1] Z clip space (OpenGL / OpenGL ES).
			const float offsetZ = nearZ;

			if ( farZ <= nearZ )
			{
				// place the far plane at infinity
				(*result)(0,0) = 2 / tanAngleWidth;
				(*result)(0,1) = 0;
				(*result)(0,2) = ( tanAngleRight + tanAngleLeft ) / tanAngleWidth;
				(*result)(0,3) = 0;

				(*result)(1,0) = 0;
				(*result)(1,1) = 2 / tanAngleHeight;
				(*result)(1,2) = ( tanAngleUp + tanAngleDown ) / tanAngleHeight;
				(*result)(1,3) = 0;

				(*result)(2,0) = 0;
				(*result)(2,1) = 0;
				(*result)(2,2) = -1;
				(*result)(2,3) = -( nearZ + offsetZ );

				(*result)(3,0) = 0;
				(*result)(3,1) = 0;
				(*result)(3,2) = -1;
				(*result)(3,3) = 0;
			}
			else
			{
				// normal projection
				(*result)(0,0) = 2 / tanAngleWidth;
				(*result)(0,1) = 0;
				(*result)(0,2) = ( tanAngleRight + tanAngleLeft ) / tanAngleWidth;
				(*result)(0,3) = 0;

				(*result)(1,0) = 0;
				(*result)(1,1) = 2 / tanAngleHeight;
				(*result)(1,2) = ( tanAngleUp + tanAngleDown ) / tanAngleHeight;
				(*result)(1,3) = 0;

				(*result)(2,0) = 0;
				(*result)(2,1) = 0;
				(*result)(2,2) = -( farZ + offsetZ ) / ( farZ - nearZ );
				(*result)(2,3) = -( farZ * ( nearZ + offsetZ ) ) / ( farZ - nearZ );

				(*result)(3,0) = 0;
				(*result)(3,1) = 0;
				(*result)(3,2) = -1;
				(*result)(3,3) = 0;
			}
		}
	};
}