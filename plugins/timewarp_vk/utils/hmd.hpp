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
        [[maybe_unused]] int   display_pixels_wide;
        int   display_pixels_high;
        int   tile_pixels_wide;
        int   tile_pixels_high;
        int   eye_tiles_wide;
        int   eye_tiles_high;
        int   visible_pixels_wide;
        int   visible_pixels_high;
        float visible_meters_wide;
        float visible_meters_high;
        float lens_separation_in_meters;
        float meters_per_tan_angle_at_center;
        int   num_knots;
        float K[11];
        float chromatic_aberration[4];
    };

    static float max_float(float x, float y);
    static float min_float(float x, float y);
    static float evaluate_catmull_rom_spline(float value, const float* K, int num_knots);
    static void  get_default_hmd_info(int display_pixels_wide, int display_pixels_high, float display_meters_wide,
                                      float display_meters_high, float lens_separation, float meters_per_tan_angle,
                                      const float aberration[4], hmd_info_t& hmd_info);
    static void
    build_distortion_meshes(std::array<std::array<std::vector<mesh_coord2d_t>, NUM_COLOR_CHANNELS>, NUM_EYES>& distort_coords,
                            hmd_info_t& hmd_info);
};
