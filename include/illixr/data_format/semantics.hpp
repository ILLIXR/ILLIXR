#pragma once

#include "illixr/switchboard.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ILLIXR::data_format {
struct semantic_data : switchboard::event {
    std::vector<uint8_t> image;
    int32_t frame_number{0};
    int32_t width{0};
    int32_t height{0};
    std::vector<uint8_t> depth;
    int32_t depth_width{0};
    int32_t depth_height{0};
    float depth_near_z{0.f};
    float intrinsics[4]{0.f, 0.f , 0.f, 0.f};
    float depth_intrinsics[4]{0.f, 0.f , 0.f, 0.f};
    float rgb_camera_pose[16]{0.f, 0.f , 0.f, 0.f, 0.f, 0.f , 0.f, 0.f, 0.f, 0.f , 0.f, 0.f, 0.f, 0.f , 0.f, 0.f};
    float depth_pose[16]{0.f, 0.f , 0.f, 0.f, 0.f, 0.f , 0.f, 0.f, 0.f, 0.f , 0.f, 0.f, 0.f, 0.f , 0.f, 0.f};
    float max_depth{0.f};

    semantic_data() = default;
};

struct voice_query : switchboard::event {
    uint64_t             query_id{0};       // for correlating response
    std::vector<uint8_t> pcm_data;       // PCM16 mono 12kHz bytes
    float                similarity_threshold{0.f};   // default 0.95
    float                min_match_similarity{0.f};   // default 0.25

    voice_query() = default;
};

struct point_cloud {
    std::vector<float> points;      // flat array of XYZ triples
    std::vector<float> centroid;    // [x, y, z]
    int32_t            num_points{0};

    point_cloud() = default;
};

struct query_response : switchboard::event {
    uint64_t                  query_id{0};        // matches voice_query::query_id
    std::vector<point_cloud>  point_clouds;
    std::vector<float>        colors;           // per-point-cloud colors
    int32_t                   num_point_clouds{0};
    float                     server_query_processing{0.f};  // latency in seconds
    std::string               text_query;       // original query echoed back

    query_response() = default;
};


} // namespace ILLIXR
