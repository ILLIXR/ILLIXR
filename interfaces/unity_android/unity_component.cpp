// Copyright 2020-2026, The Board of Trustees of the University of Illinois.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  ILLIXR Unity component implementation.
 *         Analogous to illixr_monado_create_plugin and the illixr_plugin class
 *         in illixr_component.c in the Monado driver.
 * @author RSIM Group <illixr@cs.illinois.edu>
 */

#ifdef __ANDROID__
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

#include "unity_component.hpp"

#include "illixr/data_format/query_response_ser.hpp"
#include "illixr/data_format/voice_query_ser.hpp"

#include <cstring>
#include <memory>

using namespace ILLIXR;
using namespace ILLIXR::bridge::semantic_xr;

unity_component::unity_component(const std::string& name, phonebook* pb)
        : plugin{name, pb}
        , switchboard_{pb->lookup_impl<switchboard>()}
        //, semantic_writer_{switchboard_->get_network_writer<semantic_data>("semantic_data", {})}
        , query_writer_{switchboard_->get_network_writer<voice_query>("semantic_query", {})}
        , response_reader_{switchboard_->get_reader<query_response>("semantic_response")} { }

void unity_component::send_voice_query(uint64_t       query_id_,
                                       const uint8_t* pcm_data,
                                       int32_t        pcm_len,
                                       float          similarity_threshold,
                                       float          min_match_similarity) {
    spdlog::get("illixr")->debug("Starting send for {}, pcm_pointer={}, len={}, threshold={}, similarity={}",
                                 query_id_, static_cast<void*>(const_cast<uint8_t*>(pcm_data)),
                                 pcm_len, similarity_threshold, min_match_similarity);
    auto query = std::make_shared<voice_query>();
    spdlog::get("illixr")->debug("send_voice_query: query ptr={} use_count={} pcm_data ptr={}",
                                 static_cast<void*>(query.get()),
                                 query.use_count(),
                                 static_cast<void*>(const_cast<uint8_t*>(pcm_data)));
    query->query_id_               = query_id_;
    query->similarity_threshold_   = similarity_threshold;
    query->min_match_similarity_   = min_match_similarity;
    query->pcm_data_.assign(pcm_data, pcm_data + pcm_len);


    spdlog::get("illixr")->debug("send_voice_query: query ptr={} pcm_data ptr={}",
                                 static_cast<void*>(query.get()),
                                 static_cast<void*>(const_cast<uint8_t*>(pcm_data)));
    query_writer_.put(std::move(query));
}

bool unity_component::get_query_response(uint64_t* out_query_id,
                                         float*    out_centroids,
                                         int32_t*  out_num_clouds,
                                         float*    out_colors,
                                         int32_t   out_colors_max,
                                         float*    out_server_latency,
                                         char*     out_text_query,
                                         int32_t   text_query_buf_len) {
    auto response = response_reader_.get_ro_nullable();

    if (!response)
        return false;

    // Only deliver if this is a new response we haven't seen yet
    if (response->query_id_ == last_delivered_query_id_.load())
        return false;

    last_delivered_query_id_.store(response->query_id_);

    *out_query_id       = response->query_id_;
    *out_num_clouds     = response->num_point_clouds_;
    *out_server_latency = response->server_query_processing_;

    // Copy centroids — one [x, y, z] per point cloud
    int32_t num_clouds = response->num_point_clouds_;
    for (int32_t i = 0; i < num_clouds; ++i) {
        const auto& pc = response->point_clouds_[i];
        // centroid is [x, y, z] — copy up to 3 floats defensively
        int32_t centroid_floats = static_cast<int32_t>(
            std::min(pc.centroid_.size(), static_cast<size_t>(3)));
        std::memcpy(out_centroids + i * 3,
                    pc.centroid_.data(),
                    centroid_floats * sizeof(float));
        // zero any missing components
        for (int32_t j = centroid_floats; j < 3; ++j)
            out_centroids[i * 3 + j] = 0.0f;
    }

    int32_t num_colors = static_cast<int32_t>(
        std::min(response->colors_.size(),
                 static_cast<size_t>(out_colors_max)));
    std::memcpy(out_colors, response->colors_.data(), num_colors * sizeof(float));

    // Copy text_query_ into caller-supplied buffer, null-terminated
    if (out_text_query != nullptr && text_query_buf_len > 0) {
        int32_t copy_len = static_cast<int32_t>(
            std::min(response->text_query_.size(),
                     static_cast<size_t>(text_query_buf_len - 1)));
        std::memcpy(out_text_query, response->text_query_.data(), copy_len);
        out_text_query[copy_len] = '\0';
    }

    spdlog::get("illixr")->debug("Query response id={} num_clouds={} latency={}",
                                 response->query_id_, num_clouds, response->server_query_processing_);

    return true;
}
// Static instance pointer — analogous to illixr_plugin_obj in illixr_component.c
static ILLIXR::unity_component* unity_component_obj = nullptr;

/*!
 * @brief Factory function for the Unity component.
 *        Analogous to illixr_monado_create_plugin in illixr_component.c.
 *        Called by illixr_unity_init() via runtime->load_plugin_factory().
 *        start() is not called here — load_plugin_factory() handles it.
 */
extern "C" ILLIXR::plugin* illixr_unity_create_plugin(ILLIXR::phonebook* pb) {
    unity_component_obj = new ILLIXR::unity_component{"unity_component", pb};
    return static_cast<ILLIXR::plugin*>(unity_component_obj);
}

/*!
 * @brief Sends a semantic frame to the switchboard.
 *        Called from Unity C# per frame via [DllImport("unity_bridge")].
 *        All parameters are blittable for C# marshalling — std::vector
 *        fields are passed as pointer + length pairs.
 *
 * @param frame_number     Frame counter from Unity.
 * @param width            RGB image width in pixels.
 * @param height           RGB image height in pixels.
 * @param image            Encoded RGB image bytes.
 * @param image_len        Length of image buffer in bytes.
 * @param depth_width      Depth image width in pixels.
 * @param depth_height     Depth image height in pixels.
 * @param depth            Encoded depth image bytes.
 * @param depth_len        Length of depth buffer in bytes.
 * @param depth_near_z     Near plane depth value.
 * @param intrinsics       RGB camera intrinsics [fx, fy, cx, cy].
 * @param depth_intrinsics Depth camera intrinsics [fx, fy, cx, cy].
 * @param rgb_camera_pose  RGB camera pose as row-major 4x4 matrix.
 * @param depth_pose       Depth camera pose as row-major 4x4 matrix.
 * @param max_depth        Maximum depth value.
 */
/*extern "C" void illixr_unity_send_semantic_frame(int32_t        frame_number,
                                                 int32_t        width,
                                                 int32_t        height,
                                                 const uint8_t* image,
                                                 int32_t        image_len,
                                                 int32_t        depth_width,
                                                 int32_t        depth_height,
                                                 const uint8_t* depth,
                                                 int32_t        depth_len,
                                                 float          depth_near_z,
                                                 const float*   intrinsics,
                                                 const float*   depth_intrinsics,
                                                 const float*   rgb_camera_pose,
                                                 const float*   depth_pose,
                                                 float          max_depth) {
    if (unity_component_obj == nullptr)
        return;

    unity_component_obj->send_semantic_frame(frame_number,
                                             width,
                                             height,
                                             image,
                                             image_len,
                                             depth_width,
                                             depth_height,
                                             depth,
                                             depth_len,
                                             depth_near_z,
                                             intrinsics,
                                             depth_intrinsics,
                                             rgb_camera_pose,
                                             depth_pose,
                                             max_depth);
}*/

// Called by Unity when user asks a question — writes voice_query to switchboard
extern "C" void illixr_unity_send_voice_query(uint64_t       query_id_,
                                              const uint8_t* pcm_data,
                                              int32_t        pcm_len,
                                              float          similarity_threshold,
                                              float          min_match_similarity) {
    if (unity_component_obj == nullptr)
        return;

    unity_component_obj->send_voice_query(query_id_, pcm_data, pcm_len, similarity_threshold,
                                          min_match_similarity);
}

// Called by Unity polling — reads query_response from switchboard
extern "C" int illixr_unity_get_query_response(uint64_t* out_query_id,
                                               float*    out_centroids,      // caller supplies float[num_clouds * 3]
                                               int32_t*  out_num_clouds,
                                               float*    out_colors,
                                               int32_t   out_colors_max,
                                               float*    out_server_latency,
                                               char*     out_text_query,     // caller supplies char buffer
                                               int32_t   text_query_buf_len) {
    if (unity_component_obj == nullptr)
        return 0;

    return unity_component_obj->get_query_response(out_query_id, out_centroids, out_num_clouds,
                                                   out_colors, out_colors_max,
                                                   out_server_latency, out_text_query,
                                                   text_query_buf_len) ? 1 : 0;
}

extern "C" int illixr_unity_get_query_response_info(
    uint64_t* out_query_id,
    int32_t*  out_num_clouds,
    int32_t*  out_total_points,
    int32_t*  out_points_per_cloud,
    int32_t   points_per_cloud_max,
    float*    out_centroids,
    float*    out_colors,
    int32_t   out_colors_max,
    int32_t*  out_num_colors,
    float*    out_server_latency,
    char*     out_text_query,
    int32_t   text_query_buf_len) {
    if (unity_component_obj == nullptr) return 0;
    return unity_component_obj->get_query_response_info(
               out_query_id, out_num_clouds, out_total_points,
               out_points_per_cloud, points_per_cloud_max,
               out_centroids, out_colors, out_colors_max, out_num_colors,
               out_server_latency, out_text_query, text_query_buf_len) ? 1 : 0;
}

extern "C" int illixr_unity_get_query_response_points(
    uint64_t query_id_,
    float*   out_points,
    int32_t  points_max) {
    if (unity_component_obj == nullptr) return 0;
    return unity_component_obj->get_query_response_points(
               query_id_, out_points, points_max) ? 1 : 0;
}

bool unity_component::get_query_response_info(
    uint64_t* out_query_id,
    int32_t*  out_num_clouds,
    int32_t*  out_total_points,
    int32_t*  out_points_per_cloud,
    int32_t   points_per_cloud_max,
    float*    out_centroids,
    float*    out_colors,
    int32_t   out_colors_max,
    int32_t*  out_num_colors,
    float*    out_server_latency,
    char*     out_text_query,
    int32_t   text_query_buf_len) {

    auto response = response_reader_.get_ro_nullable();
    if (!response)
        return false;

    if (response->query_id_ == last_delivered_query_id_.load())
        return false;

    last_delivered_query_id_.store(response->query_id_);
    cached_response_ = response;   // cache for second call

    *out_query_id       = response->query_id_;
    *out_num_clouds     = response->num_point_clouds_;
    *out_server_latency = response->server_query_processing_;

    // Count total points across all clouds
    int32_t total_points = 0;
    for (int32_t i = 0; i < response->num_point_clouds_; ++i)
        total_points += response->point_clouds_[i].num_points_;
    *out_total_points = total_points;

    // Points per cloud array
    int32_t num_clouds = std::min(response->num_point_clouds_,
                                  points_per_cloud_max);
    for (int32_t i = 0; i < num_clouds; ++i)
        out_points_per_cloud[i] = response->point_clouds_[i].num_points_;

    // Centroids — [x,y,z] per cloud
    for (int32_t i = 0; i < num_clouds; ++i) {
        const auto& pc = response->point_clouds_[i];
        int32_t n = static_cast<int32_t>(
            std::min(pc.centroid_.size(), static_cast<size_t>(3)));
        std::memcpy(out_centroids + i * 3, pc.centroid_.data(),
                    n * sizeof(float));
        for (int32_t j = n; j < 3; ++j)
            out_centroids[i * 3 + j] = 0.0f;
    }

    // Colors — 3 floats per cloud
    int32_t num_colors = static_cast<int32_t>(
        std::min(response->colors_.size(),
                 static_cast<size_t>(out_colors_max)));
    std::memcpy(out_colors, response->colors_.data(),
                num_colors * sizeof(float));
    *out_num_colors = num_colors;

    // Text query
    if (out_text_query != nullptr && text_query_buf_len > 0) {
        int32_t copy_len = static_cast<int32_t>(
            std::min(response->text_query_.size(),
                     static_cast<size_t>(text_query_buf_len - 1)));
        std::memcpy(out_text_query, response->text_query_.data(), copy_len);
        out_text_query[copy_len] = '\0';
    }

    spdlog::get("illixr")->debug(
        "get_query_response_info: id={} clouds={} total_points={}",
        response->query_id_, response->num_point_clouds_, total_points);

    return true;
}

bool unity_component::get_query_response_points(
    uint64_t query_id_,
    float*   out_points,
    int32_t  points_max) {

    if (!cached_response_ || cached_response_->query_id_ != query_id_)
        return false;

    int32_t written = 0;
    for (const auto& pc : cached_response_->point_clouds_) {
        int32_t n = static_cast<int32_t>(
            std::min(static_cast<size_t>(pc.num_points_),
                     pc.points_.size() / 3));
        int32_t floats = n * 3;
        if (written + floats > points_max * 3) break;
        std::memcpy(out_points + written,
                    pc.points_.data(),
                    floats * sizeof(float));
        written += floats;
    }

    spdlog::get("illixr")->debug(
        "get_query_response_points: id={} written={} floats",
        query_id_, written);

    return true;
}
#endif // __ANDROID__
