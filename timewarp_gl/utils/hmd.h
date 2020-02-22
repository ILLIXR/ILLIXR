#ifndef _HMD_H
#define _HMD_H

#include <GL/glut.h>
#include <GL/freeglut.h>


typedef struct
{
	GLfloat x;
	GLfloat y;
} mesh_coord2d_t;

typedef struct
{
	GLfloat x;
	GLfloat y;
	GLfloat z;
} mesh_coord3d_t;

typedef struct
{
	GLfloat u;
	GLfloat v;
} uv_coord_t;


typedef struct
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
} hmd_info_t;

typedef struct
{
	float	interpupillaryDistance;
} body_info_t;

int foo();

float MaxFloat( const float x, const float y );
float MinFloat( const float x, const float y );

float EvaluateCatmullRomSpline( float value, float* K, int numKnots );
void GetDefaultHmdInfo( const int displayPixelsWide, const int displayPixelsHigh, hmd_info_t* hmd_info);
void GetDefaultBodyInfo(body_info_t* body_info);

#endif