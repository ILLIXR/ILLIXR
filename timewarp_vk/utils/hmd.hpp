#ifndef _HMD_H
#define _HMD_H

#include <array>
#include <vector>

// HMD utility class for warp mesh structs, spline math, etc
class HMD {
public:
    static constexpr int NUM_EYES           = 2;
    static constexpr int NUM_COLOR_CHANNELS = 3;

    struct mesh_coord2d_t {
        float x;
        float y;
    };

    struct mesh_coord3d_t {
        float x;
        float y;
        float z;
    };

    struct uv_coord_t {
        float u;
        float v;
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

    static float MaxFloat(const float x, const float y);
    static float MinFloat(const float x, const float y);
    static float EvaluateCatmullRomSpline(float value, float* K, int numKnots);
    static void  GetDefaultHmdInfo(const int displayPixelsWide, const int displayPixelsHigh, const float displayMetersWide,
                                   const float displayMetersHigh, const float lensSeparation, const float metersPerTanAngle,
                                   const float aberration[4], hmd_info_t& hmd_info);
    static void
    BuildDistortionMeshes(std::array<std::array<std::vector<mesh_coord2d_t>, NUM_COLOR_CHANNELS>, NUM_EYES>& distort_coords,
                          hmd_info_t&                                                                        hmdInfo);
};

#endif
