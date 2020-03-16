#ifndef _HMD_H
#define _HMD_H

#include <GL/gl.h>


// HMD utility class for warp mesh structs, spline math, etc
class HMD {

public:

	static constexpr int NUM_EYES = 2;
	static constexpr int NUM_COLOR_CHANNELS = 3;

	struct mesh_coord2d_t
	{
		GLfloat x;
		GLfloat y;
	};

	struct mesh_coord3d_t
	{
		GLfloat x;
		GLfloat y;
		GLfloat z;
	};

	struct uv_coord_t
	{
		GLfloat u;
		GLfloat v;
	};

	struct hmd_info_t
	{
		int		displayPixelsWide;
		int		displayPixelsHigh;
		int		tilePixelsWide;
		int		tilePixelsHigh;
		int		eyeTilesWide;
		int		eyeTilesHigh;
		int		visiblePixelsWide;
		int		visiblePixelsHigh;
		float	visibleMetersWide;
		float	visibleMetersHigh;
		float	lensSeparationInMeters;
		float	metersPerTanAngleAtCenter;
		int		numKnots;
		float	K[11];
		float	chromaticAberration[4];
	};

	struct body_info_t
	{
		float	interpupillaryDistance;
	};

	static float MaxFloat( const float x, const float y );
	static float MinFloat( const float x, const float y );

	static float EvaluateCatmullRomSpline( float value, float* K, int numKnots );
	static void GetDefaultHmdInfo( const int displayPixelsWide, const int displayPixelsHigh, hmd_info_t* hmd_info);
	static void GetDefaultBodyInfo(body_info_t* body_info);

	static void BuildDistortionMeshes( mesh_coord2d_t * distort_coords[NUM_EYES][NUM_COLOR_CHANNELS], hmd_info_t * hmdInfo );

};

#endif