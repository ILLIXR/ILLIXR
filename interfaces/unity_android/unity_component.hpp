// Copyright 2020-2026, The Board of Trustees of the University of Illinois.
// SPDX-License-Identifier: BSL-1.0
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

#include "illixr/data_format/semantics.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

namespace ILLIXR {

class unity_component : public plugin {
public:
    explicit unity_component(const std::string& name, phonebook* pb);
    ~unity_component() override = default;

    /*!
     * @brief Constructs a semantic_data object from raw C-compatible parameters
     *        and writes it to the switchboard.
     *        Called from the C-linkage illixr_unity_send_semantic_frame function.
     */
    void send_semantic_frame(int32_t        frame_number,
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
                             float          max_depth);

   void send_voice_query(uint64_t       query_id,
                         const uint8_t* pcm_data,
                         int32_t        pcm_len,
                         float          similarity_threshold,
                         float          min_match_similarity);

    bool get_query_response(uint64_t* out_query_id,
                            float*    out_centroids,
                            int32_t*  out_num_clouds,
                            float*    out_server_latency,
                            char*     out_text_query,
                            int32_t   text_query_buf_len);

private:
    const std::shared_ptr<switchboard>  switchboard_;
    switchboard::network_writer<data_format::semantic_data> semantic_writer_;
    switchboard::network_writer<data_format::voice_query>   query_writer_;
    switchboard::reader<data_format::query_response>        response_reader_;

    // Sequence number of the last query_response we delivered to Unity,
    // used to detect when a new response has arrived.
    std::atomic<uint64_t> last_delivered_query_id_{0};
};

} // namespace ILLIXR

#endif // __ANDROID__
