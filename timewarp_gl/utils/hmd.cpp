#include <cmath>
#include "hmd.hpp"


float HMD::MaxFloat( const float x, const float y ) { return ( x > y ) ? x : y; }
float HMD::MinFloat( const float x, const float y ) { return ( x < y ) ? x : y; }

// A Catmull-Rom spline through the values K[0], K[1], K[2] ... K[numKnots-1] evenly spaced from 0.0 to 1.0
float HMD::EvaluateCatmullRomSpline( float value, float * K, int numKnots )
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

void HMD::BuildDistortionMeshes( mesh_coord2d_t * distort_coords[NUM_EYES][NUM_COLOR_CHANNELS], hmd_info_t * hmdInfo )
{
	const float horizontalShiftMeters = ( hmdInfo->lensSeparationInMeters / 2 ) - ( hmdInfo->visibleMetersWide / 4 );
	const float horizontalShiftView = horizontalShiftMeters / ( hmdInfo->visibleMetersWide / 2 );

	for ( int eye = 0; eye < NUM_EYES; eye++ )
	{
		for ( int y = 0; y <= hmdInfo->eyeTilesHigh; y++ )
		{
			const float yf = 1.0f - (float)y / (float)hmdInfo->eyeTilesHigh;

			for ( int x = 0; x <= hmdInfo->eyeTilesWide; x++ )
			{
				const float xf = (float)x / (float)hmdInfo->eyeTilesWide;

				const float in[2] = { ( eye ? -horizontalShiftView : horizontalShiftView ) + xf, yf };
				const float ndcToPixels[2] = { hmdInfo->visiblePixelsWide * 0.25f, hmdInfo->visiblePixelsHigh * 0.5f };
				const float pixelsToMeters[2] = { hmdInfo->visibleMetersWide / hmdInfo->visiblePixelsWide, hmdInfo->visibleMetersHigh / hmdInfo->visiblePixelsHigh };

				float theta[2];
				for ( int i = 0; i < 2; i++ )
				{
					const float unit = in[i];
					const float ndc = 2.0f * unit - 1.0f;
					const float pixels = ndc * ndcToPixels[i];
					const float meters = pixels * pixelsToMeters[i];
					const float tanAngle = meters / hmdInfo->metersPerTanAngleAtCenter;
					theta[i] = tanAngle;
				}

				const float rsq = theta[0] * theta[0] + theta[1] * theta[1];
				const float scale = HMD::EvaluateCatmullRomSpline( rsq, hmdInfo->K, hmdInfo->numKnots );
				const float chromaScale[] =
				{
					scale * ( 1.0f + hmdInfo->chromaticAberration[0] + rsq * hmdInfo->chromaticAberration[1] ),
					scale,
					scale * ( 1.0f + hmdInfo->chromaticAberration[2] + rsq * hmdInfo->chromaticAberration[3] )
				};

				const int vertNum = y * ( hmdInfo->eyeTilesWide + 1 ) + x;
				for ( int channel = 0; channel < NUM_COLOR_CHANNELS; channel++ )
				{
					distort_coords[eye][channel][vertNum].x = chromaScale[channel] * theta[0];
					distort_coords[eye][channel][vertNum].y = chromaScale[channel] * theta[1];
				}
			}
		}
	}
}

void HMD::GetDefaultHmdInfo( const int displayPixelsWide, const int displayPixelsHigh, hmd_info_t* hmd_info)
{
	hmd_info->displayPixelsWide = displayPixelsWide;
	hmd_info->displayPixelsHigh = displayPixelsHigh;
	hmd_info->tilePixelsWide = 8;
	hmd_info->tilePixelsHigh = 8;
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

void HMD::GetDefaultBodyInfo(body_info_t* body_info)
{
	body_info->interpupillaryDistance	= 0.0640f;	// average interpupillary distance
}
