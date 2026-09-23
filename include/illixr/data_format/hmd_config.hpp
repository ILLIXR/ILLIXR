#pragma once

#ifdef __cplusplus
#    include "illixr/switchboard.hpp"

#    include <cstdint>
#else
#    include <stdint.h>
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

struct ipd : public switchboard::event {
    float      ipd_mm;
    float      ipd_m;
    time_point time;

    ipd(const float ipd_)
        : ipd_mm{ipd_/ 1000.f}
        , ipd_m{ipd_}
        , time{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}}{}

    ipd& operator=(const ipd& ipd_in) {
        if (this == &ipd_in)
            return *this;
        ipd_mm = ipd_in.ipd_mm;
        ipd_m = ipd_in.ipd_m;
        time = ipd_in.time;
        return *this;
    }

    ipd(const ipd& ipd_in)
        : ipd_mm{ipd_in.ipd_mm}
        , ipd_m{ipd_in.ipd_m}
        , time{ipd_in.time} {}
};

struct hmd_config_data : public switchboard::event {
    hmd_config config;
    float      ipd{64.f};

    hmd_config get_cfg(float overscan = 1.f, bool double_width = false) const {
        hmd_config cfg;
        if (double_width) {
            cfg.recommended_image_width = (uint32_t) (config.recommended_image_width * overscan * 2.f);
        } else {
            cfg.recommended_image_width = (uint32_t) (config.recommended_image_width * overscan);
        }
        cfg.recommended_image_height = (uint32_t) (config.recommended_image_height * overscan);

        for (auto i = 0; i < 2; i++) {
            cfg.fov_angle_left[i]  = config.fov_angle_left[i] * overscan;
            cfg.fov_angle_right[i] = config.fov_angle_right[i] * overscan;
            cfg.fov_angle_up[i]    = config.fov_angle_up[i] * overscan;
            cfg.fov_angle_down[i]  = config.fov_angle_down[i] * overscan;
        }
        return cfg;
    }

    hmd_config_data()
        : config{}
        , ipd{64.} {}

    hmd_config_data(const hmd_config cfg, const float ip)
        : config{cfg}
        , ipd{ip} {}
};
} // namespace ILLIXR::data_format
#endif
