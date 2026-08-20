#pragma once

#ifdef __cplusplus
    #include "illixr/switchboard.hpp"

    #include <cstdint>

#else
    #include <stdint.h>
#endif
struct hmd_config {
    uint32_t recommended_image_width;
    uint32_t recommended_image_height;

    float fov_angle_left[2];
    float fov_angle_right[2];
    float fov_angle_up[2];
    float fov_angle_down[2];
};

#ifdef __cplusplus
namespace ILLIXR::data_format {

struct hmd_config_data : public switchboard::event {
    hmd_config config;

    void get_cfg(hmd_config& cfg, float overscan = 1.f) const {
        cfg.recommended_image_width  = (uint32_t) (config.recommended_image_width * overscan);
        cfg.recommended_image_height = (uint32_t) (config.recommended_image_height * overscan);

        for (auto i = 0; i < 2; i++) {
            cfg.fov_angle_left[i]  = config.fov_angle_left[i] * overscan;
            cfg.fov_angle_right[i] = config.fov_angle_right[i] * overscan;
            cfg.fov_angle_up[i]    = config.fov_angle_up[i] * overscan;
            cfg.fov_angle_down[i]  = config.fov_angle_down[i] * overscan;
        }
    }
};
} // namespace ILLIXR::data_format
#endif
