#pragma once
#include <array>
#include <vector>

#ifdef USE_GL
    #include <GL/gl.h>
    #define FLOAT GLfloat
#else
    #define FLOAT float
#endif
// HMD utility class for warp mesh structs, spline math, etc
class HMD {
public:
    static constexpr int NUM_EYES           = 2;
    static constexpr int NUM_COLOR_CHANNELS = 3;

    struct mesh_coord2d_t {
        FLOAT x;
        FLOAT y;
    };

    struct mesh_coord3d_t {
        FLOAT x;
        FLOAT y;
        FLOAT z;
    };

    struct uv_coord_t {
        FLOAT u;
        FLOAT v;
    };

    struct hmd_info_t {
        int   displayPixelsWide;
        int   displayPixelsHigh;
        int   tilePixelsWide;
        int   tilePixelsHigh;
        int   eyeTilesWide;
        int   eyeTilesHigh;
        int   visiblePixelsWide;
        int   visiblePixelsHigh;
        float visibleMetersWide;
        float visibleMetersHigh;
        float lensSeparationInMeters;
        float metersPerTanAngleAtCenter;
        int   numKnots;
        float K[11];
        float chromaticAberration[4];
    };

    static float MaxFloat(float x, float y);
    static float MinFloat(float x, float y);
    static float EvaluateCatmullRomSpline(float value, const float* K, int numKnots);
    static void  GetDefaultHmdInfo(int displayPixelsWide, int displayPixelsHigh, float displayMetersWide,
                                   float displayMetersHigh, float lensSeparation, float metersPerTanAngle,
                                   const float aberration[4], hmd_info_t& hmd_info);
    static void
    BuildDistortionMeshes(std::array<std::array<std::vector<mesh_coord2d_t>, NUM_COLOR_CHANNELS>, NUM_EYES>& distort_coords,
                          hmd_info_t&                                                                        hmdInfo);
};
