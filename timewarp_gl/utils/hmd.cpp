#include <cmath>
#include "hmd.h"

const int   NUM_EYES        = 2;
const int   NUM_COLOR_CHANNELS = 3;

float MaxFloat( const float x, const float y ) { return ( x > y ) ? x : y; }
float MinFloat( const float x, const float y ) { return ( x < y ) ? x : y; }

// A Catmull-Rom spline through the values K[0], K[1], K[2] ... K[numKnots-1] evenly spaced from 0.0 to 1.0
float EvaluateCatmullRomSpline( float value, float * K, int numKnots )
{
	const float scaledValue = (float)( numKnots - 1 ) * value;
	const float scaledValueFloor = MaxFloat( 0.0f, MinFloat( (float)( numKnots - 1 ), floorf( scaledValue ) ) );
	const float t = scaledValue - scaledValueFloor;
	const int k = (int)scaledValueFloor;

	float p0 = 0.0f;
	float p1 = 0.0f;
	float m0 = 0.0f;
	float m1 = 0.0f;

	if ( k == 0 )
	{
		p0 = K[0];
		m0 = K[1] - K[0];
		p1 = K[1];
		m1 = 0.5f * ( K[2] - K[0] );
	}
	else if ( k < numKnots - 2 )
	{
		p0 = K[k];
		m0 = 0.5f * ( K[k+1] - K[k-1] );
		p1 = K[k+1];
		m1 = 0.5f * ( K[k+2] - K[k] );
	}
	else if ( k == numKnots - 2 )
	{
		p0 = K[k];
		m0 = 0.5f * ( K[k+1] - K[k-1] );
		p1 = K[k+1];
		m1 = K[k+1] - K[k];
	}
	else if ( k == numKnots - 1 )
	{
		p0 = K[k];
		m0 = K[k] - K[k-1];
		p1 = p0 + m0;
		m1 = m0;
	}

	const float omt = 1.0f - t;
	const float res = ( p0 * ( 1.0f + 2.0f *   t ) + m0 *   t ) * omt * omt
					+ ( p1 * ( 1.0f + 2.0f * omt ) - m1 * omt ) *   t *   t;
	return res;
}

void GetDefaultHmdInfo( const int displayPixelsWide, const int displayPixelsHigh, hmd_info_t* hmd_info)
{
	hmd_info->displayPixelsWide = displayPixelsWide;
	hmd_info->displayPixelsHigh = displayPixelsHigh;
	hmd_info->tilePixelsWide = 32;
	hmd_info->tilePixelsHigh = 32;
	hmd_info->eyeTilesWide = displayPixelsWide / hmd_info->tilePixelsWide / NUM_EYES;
	hmd_info->eyeTilesHigh = displayPixelsHigh / hmd_info->tilePixelsHigh;
	hmd_info->visiblePixelsWide = hmd_info->eyeTilesWide * hmd_info->tilePixelsWide * NUM_EYES;
	hmd_info->visiblePixelsHigh = hmd_info->eyeTilesHigh * hmd_info->tilePixelsHigh;
	hmd_info->visibleMetersWide = 0.11047f * ( hmd_info->eyeTilesWide * hmd_info->tilePixelsWide * NUM_EYES ) / displayPixelsWide;
	hmd_info->visibleMetersHigh = 0.06214f * ( hmd_info->eyeTilesHigh * hmd_info->tilePixelsHigh ) / displayPixelsHigh;
	hmd_info->lensSeparationInMeters = hmd_info->visibleMetersWide / NUM_EYES;
	hmd_info->metersPerTanAngleAtCenter = 0.037f;
	hmd_info->numKnots = 11;
	hmd_info->K[0] = 1.0f;
	hmd_info->K[1] = 1.021f;
	hmd_info->K[2] = 1.051f;
	hmd_info->K[3] = 1.086f;
	hmd_info->K[4] = 1.128f;
	hmd_info->K[5] = 1.177f;
	hmd_info->K[6] = 1.232f;
	hmd_info->K[7] = 1.295f;
	hmd_info->K[8] = 1.368f;
	hmd_info->K[9] = 1.452f;
	hmd_info->K[10] = 1.560f;
	hmd_info->chromaticAberration[0] = -0.016f;
	hmd_info->chromaticAberration[1] =  0.0f;
	hmd_info->chromaticAberration[2] =  0.024f;
	hmd_info->chromaticAberration[3] =  0.0f;
}

void GetDefaultBodyInfo(body_info_t* body_info)
{
	body_info->interpupillaryDistance	= 0.0640f;	// average interpupillary distance
}

/*
static void ksGpuTriangleIndexArray_CreateFromBuffer( ksGpuTriangleIndexArray * indices, const int indexCount, const ksGpuBuffer * buffer )
{
	indices->indexCount = indexCount;
	indices->indexArray = NULL;
	indices->buffer = buffer;
}

static void ksGpuTriangleIndexArray_Alloc( ksGpuTriangleIndexArray * indices, const int indexCount, const ksGpuTriangleIndex * data )
{
	indices->indexCount = indexCount;
	indices->indexArray = (ksGpuTriangleIndex *) malloc( indexCount * sizeof( ksGpuTriangleIndex ) );
	if ( data != NULL )
	{
		memcpy( indices->indexArray, data, indexCount * sizeof( ksGpuTriangleIndex ) );
	}
	indices->buffer = NULL;
}

static void ksGpuTriangleIndexArray_Free( ksGpuTriangleIndexArray * indices )
{
	free( indices->indexArray );
	memset( indices, 0, sizeof( ksGpuTriangleIndexArray ) );
}*/