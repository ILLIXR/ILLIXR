// Copyright 2020-2026, The Board of Trustees of the University of Illinois.
/*!
 * @file
 * @brief  ILLIXR Unity component.
 *         Analogous to illixr_plugin in illixr_component.c in the Monado driver:
 *         the ILLIXR-side component that owns switchboard handles accessible to
 *         the Unity interface. Switchboard readers and writers for additional
 *         topics are added here as they are defined.
 * @author RSIM Group <illixr@cs.illinois.edu>
 */

#pragma once

#ifdef __ANDROID__

#    include "illixr/data_format/query_response.hpp"
#    include "illixr/data_format/voice_query.hpp"
#    include "illixr/phonebook.hpp"
#    include "illixr/plugin.hpp"
#    include "illixr/switchboard.hpp"

namespace ILLIXR {

class unity_component : public plugin {
public:
    explicit unity_component(const std::string& name, phonebook* pb);
    ~unity_component() override = default;

    void send_voice_query(uint64_t query_id, const uint8_t* pcm_data, int32_t pcm_len, float similarity_threshold,
                          float min_match_similarity);

    bool get_query_response(uint64_t* out_query_id, float* out_centroids, int32_t* out_num_clouds, float* out_colors,
                            int32_t out_colors_max, float* out_server_latency, char* out_text_query,
                            int32_t text_query_buf_len);

    bool get_query_response_info(uint64_t* out_query_id, int32_t* out_num_clouds, int32_t* out_total_points,
                                 int32_t* out_points_per_cloud, int32_t points_per_cloud_max, float* out_centroids,
                                 float* out_colors, int32_t out_colors_max, int32_t* out_num_colors, float* out_server_latency,
                                 char* out_text_query, int32_t text_query_buf_len);

    bool get_query_response_points(uint64_t query_id, float* out_points, int32_t points_max);

private:
    const std::shared_ptr<switchboard> switchboard_;
    // switchboard::network_writer<data_format::semantic_data> semantic_writer_;
    switchboard::network_writer<data_format::semantic_xr::voice_query> query_writer_;
    switchboard::reader<data_format::semantic_xr::query_response>      response_reader_;

    // Sequence number of the last query_response we delivered to Unity,
    // used to detect when a new response has arrived.
    std::atomic<uint64_t>                                           last_delivered_query_id_{0};
    std::shared_ptr<const data_format::semantic_xr::query_response> cached_response_;
};

} // namespace ILLIXR

#endif // __ANDROID__
