#include "illixr/hmd.hpp"

#include <cmath>

float HMD::max_float(const float x, const float y) {
    return (x > y) ? x : y;
}

float HMD::min_float(const float x, const float y) {
    return (x < y) ? x : y;
}

// A Catmull-Rom spline through the values K[0], K[1], K[2] ... K[num_knots-1] evenly spaced from 0.0 to 1.0
float HMD::evaluate_catmull_rom_spline(float value, const float* K, int num_knots) {
    const float scaled_value       = (float) (num_knots - 1) * value;
    const float scaled_value_floor = max_float(0.0f, min_float((float) (num_knots - 1), floorf(scaled_value)));
    const float t                  = scaled_value - scaled_value_floor;
    const int   k                  = (int) scaled_value_floor;

    float p0 = 0.0f;
    float p1 = 0.0f;
    float m0 = 0.0f;
    float m1 = 0.0f;

    if (k == 0) {
        p0 = K[0];
        m0 = K[1] - K[0];
        p1 = K[1];
        m1 = 0.5f * (K[2] - K[0]);
    } else if (k < num_knots - 2) {
        p0 = K[k];
        m0 = 0.5f * (K[k + 1] - K[k - 1]);
        p1 = K[k + 1];
        m1 = 0.5f * (K[k + 2] - K[k]);
    } else if (k == num_knots - 2) {
        p0 = K[k];
        m0 = 0.5f * (K[k + 1] - K[k - 1]);
        p1 = K[k + 1];
        m1 = K[k + 1] - K[k];
    } else if (k == num_knots - 1) {
        p0 = K[k];
        m0 = K[k] - K[k - 1];
        p1 = p0 + m0;
        m1 = m0;
    }

    const float omt = 1.0f - t;
    const float res = (p0 * (1.0f + 2.0f * t) + m0 * t) * omt * omt + (p1 * (1.0f + 2.0f * omt) - m1 * omt) * t * t;
    return res;
}

[[maybe_unused]] void
HMD::build_distortion_meshes(std::array<std::array<std::vector<mesh_coord2d_t>, NUM_COLOR_CHANNELS>, NUM_EYES>& distort_coords,
                             hmd_info_t&                                                                        hmd_info) {
    const float horizontal_shift_meters = (hmd_info.lens_separation_in_meters / 2) - (hmd_info.visible_meters_wide / 4);
    const float horizontal_shift_view   = horizontal_shift_meters / (hmd_info.visible_meters_wide / 2);

    bool compare_images = switchboard_->get_env_bool("ILLIXR_COMPARE_IMAGES");

    for (int eye = 0; eye < NUM_EYES; eye++) {
        for (int y = 0; y <= hmd_info.eye_tiles_high; y++) {
            const float yf = 1.0f - (float) y / (float) hmd_info.eye_tiles_high;

            for (int x = 0; x <= hmd_info.eye_tiles_wide; x++) {
                const float xf = (float) x / (float) hmd_info.eye_tiles_wide;

                const float in[2]               = {(eye ? -horizontal_shift_view : horizontal_shift_view) + xf, yf};
                const float ndc_to_pixels[2]    = {static_cast<float>(hmd_info.visible_pixels_wide) * 0.25f,
                                                   static_cast<float>(hmd_info.visible_pixels_high) * 0.5f};
                const float pixels_to_meters[2] = {
                    hmd_info.visible_meters_wide / static_cast<float>(hmd_info.visible_pixels_wide),
                    hmd_info.visible_meters_high / static_cast<float>(hmd_info.visible_pixels_high)};

                float theta[2];
                for (int i = 0; i < 2; i++) {
                    const float unit      = in[i];
                    const float ndc       = 2.0f * unit - 1.0f;
                    const float pixels    = ndc * ndc_to_pixels[i];
                    const float meters    = pixels * pixels_to_meters[i];
                    const float tan_angle = meters / hmd_info.meters_per_tan_angle_at_center;
                    theta[i]              = tan_angle;
                }

                const float rsq            = theta[0] * theta[0] + theta[1] * theta[1];
                const float scale          = HMD::evaluate_catmull_rom_spline(rsq, hmd_info.K, hmd_info.num_knots);
                const float chroma_scale[] = {
                    scale * (1.0f + hmd_info.chromatic_aberration[0] + rsq * hmd_info.chromatic_aberration[1]), scale,
                    scale * (1.0f + hmd_info.chromatic_aberration[2] + rsq * hmd_info.chromatic_aberration[3])};

                const int vert_num = y * (hmd_info.eye_tiles_wide + 1) + x;
                for (int channel = 0; channel < NUM_COLOR_CHANNELS; channel++) {
                    if (compare_images) {
                        distort_coords[eye][channel][vert_num].x = theta[0];
                        distort_coords[eye][channel][vert_num].y = theta[1];
                    } else {
                        distort_coords[eye][channel][vert_num].x = chroma_scale[channel] * theta[0];
                        distort_coords[eye][channel][vert_num].y = chroma_scale[channel] * theta[1];
                    }
                }
            }
        }
    }
}

[[maybe_unused]] void HMD::get_default_hmd_info(int display_pixels_wide, int display_pixels_high, float display_meters_wide,
                                                float display_meters_high, float lens_separation, float meters_per_tan_angle,
                                                const float aberration[4], hmd_info_t& hmd_info) {
    hmd_info.display_pixels_wide = display_pixels_wide;
    hmd_info.display_pixels_high = display_pixels_high;
    hmd_info.tile_pixels_wide    = 32;
    hmd_info.tile_pixels_high    = 32;
    hmd_info.eye_tiles_wide      = display_pixels_wide / hmd_info.tile_pixels_wide / NUM_EYES;
    hmd_info.eye_tiles_high      = display_pixels_high / hmd_info.tile_pixels_high;
    hmd_info.visible_pixels_wide = hmd_info.eye_tiles_wide * hmd_info.tile_pixels_wide * NUM_EYES;
    hmd_info.visible_pixels_high = hmd_info.eye_tiles_high * hmd_info.tile_pixels_high;
    hmd_info.visible_meters_wide = display_meters_wide *
        static_cast<float>(hmd_info.eye_tiles_wide * hmd_info.tile_pixels_wide * NUM_EYES) /
        static_cast<float>(display_pixels_wide);
    hmd_info.visible_meters_high = display_meters_high *
        static_cast<float>(hmd_info.eye_tiles_high * hmd_info.tile_pixels_high) / static_cast<float>(display_pixels_high);
    hmd_info.lens_separation_in_meters      = lens_separation;
    hmd_info.meters_per_tan_angle_at_center = meters_per_tan_angle;
    hmd_info.num_knots                      = 11;
    hmd_info.K[0]                           = 1.0f;
    hmd_info.K[1]                           = 1.021f;
    hmd_info.K[2]                           = 1.051f;
    hmd_info.K[3]                           = 1.086f;
    hmd_info.K[4]                           = 1.128f;
    hmd_info.K[5]                           = 1.177f;
    hmd_info.K[6]                           = 1.232f;
    hmd_info.K[7]                           = 1.295f;
    hmd_info.K[8]                           = 1.368f;
    hmd_info.K[9]                           = 1.452f;
    hmd_info.K[10]                          = 1.560f;
    hmd_info.chromatic_aberration[0]        = aberration[0];
    hmd_info.chromatic_aberration[1]        = aberration[1];
    hmd_info.chromatic_aberration[2]        = aberration[2];
    hmd_info.chromatic_aberration[3]        = aberration[3];
}
