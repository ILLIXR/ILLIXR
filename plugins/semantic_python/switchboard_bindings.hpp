/*!
 * @file
 * @brief  pybind11 bindings exposing ILLIXR switchboard topics to Python.
 *
 *         Three proxy structs are registered as Python classes:
 *
 *           SemanticDataReader  - wraps switchboard::reader<semantic_data>
 *             .get() -> dict or None
 *               "image"           numpy uint8 (N,)      encoded RGB bytes
 *               "depth"           numpy uint8 (N,)      encoded depth bytes
 *               "frame_number"    int
 *               "width"           int
 *               "height"          int
 *               "depth_width"     int
 *               "depth_height"    int
 *               "depth_near_z"    float
 *               "max_depth"       float
 *               "intrinsics"      numpy float32 (4,)    [fx,fy,cx,cy]
 *               "depth_intrinsics"numpy float32 (4,)    [fx,fy,cx,cy]
 *               "rgb_camera_pose" numpy float32 (4,4)   row-major
 *               "depth_pose"      numpy float32 (4,4)   row-major
 *
 *           VoiceQueryReader    - wraps switchboard::reader<voice_query>
 *             .get() -> dict or None
 *               "query_id"              int
 *               "pcm_data"              numpy uint8 (N,)  PCM16 mono 12kHz
 *               "similarity_threshold"  float
 *               "min_match_similarity"  float
 *
 *           QueryResponseWriter - wraps switchboard::writer<query_response>
 *             .put(query_id, point_clouds, colors, server_latency, text_query)
 *               point_clouds: list of dicts, each with
 *                 "points"   list[float]  flat XYZ triples
 *                 "centroid" list[float]  [x, y, z]
 *
 * @author RSIM Group <illixr@cs.illinois.edu>
 */
#pragma once

#include "illixr/data_format/semantics.hpp"
#include "illixr/switchboard.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <memory>
#include <string>

namespace ILLIXR {

static pybind11::array_t<uint8_t> to_numpy_flat_safe(std::shared_ptr<const data_format::semantic_data> owner,
                                                     const uint8_t* data, size_t size) {
    pybind11::capsule base(new std::shared_ptr<const data_format::semantic_data>(owner), [](void* p) {
        delete static_cast<std::shared_ptr<const data_format::semantic_data>*>(p);
    });

    return pybind11::array_t<uint8_t>({static_cast<pybind11::ssize_t>(size)}, {static_cast<pybind11::ssize_t>(1)}, data, base);
}

static pybind11::array_t<float> to_numpy_1d_safe(std::shared_ptr<const data_format::semantic_data> owner, const float* data,
                                                 pybind11::size_t size) {
    pybind11::capsule base(new std::shared_ptr<const data_format::semantic_data>(owner), [](void* p) {
        delete static_cast<std::shared_ptr<const data_format::semantic_data>*>(p);
    });

    return pybind11::array_t<float>({size}, {static_cast<pybind11::size_t>(sizeof(float))}, data, base);
}

static pybind11::array_t<float> to_numpy_4x4_safe(std::shared_ptr<const data_format::semantic_data> owner, const float* data) {
    pybind11::capsule base(new std::shared_ptr<const data_format::semantic_data>(owner), [](void* p) {
        delete static_cast<std::shared_ptr<const data_format::semantic_data>*>(p);
    });

    return pybind11::array_t<float>(
        {static_cast<pybind11::ssize_t>(4), static_cast<pybind11::ssize_t>(4)},
        {static_cast<pybind11::ssize_t>(sizeof(float) * 4), static_cast<pybind11::ssize_t>(sizeof(float))}, data, base);
}

struct py_semantic_data_reader {
    switchboard::reader<data_format::semantic_data>* reader;

    [[nodiscard]] pybind11::object get() const {
        auto val = reader->get_ro_nullable();
        if (!val)
            return pybind11::none();

        pybind11::dict data;
        data["image"]              = to_numpy_flat_safe(val, val->image.data(), val->image.size());
        data["frame_number"]       = val->frame_number;
        data["image_width"]        = val->width;
        data["image_height"]       = val->height;
        data["depth"]              = to_numpy_flat_safe(val, val->depth.data(), val->depth.size());
        data["depth_width"]        = val->depth_width;
        data["depth_height"]       = val->depth_height;
        data["depth_near_z"]       = val->depth_near_z;
        data["intrinsics"]         = to_numpy_1d_safe(val, val->intrinsics, 4);
        data["depth_intrinsics"]   = to_numpy_1d_safe(val, val->depth_intrinsics, 4);
        data["rgb_camera_pose"]    = to_numpy_4x4_safe(val, val->rgb_camera_pose);
        data["depth_pose"]         = to_numpy_4x4_safe(val, val->depth_pose);
        data["max_depth_m"]        = val->max_depth;

        return data;
    }
};

// -----------------------------------------------------------------------
// VoiceQueryReader
// -----------------------------------------------------------------------

struct py_voice_query_reader {
    switchboard::reader<data_format::voice_query>* reader_;
    uint64_t                          last_query_id_ = 0;

    pybind11::object get() {
        auto val = reader_->get_ro_nullable();
        if (!val)
            return pybind11::none();

        // Only return if this is a new query we haven't delivered yet
        if (val->query_id == last_query_id_)
            return pybind11::none();

        last_query_id_ = val->query_id;

        pybind11::dict d;
        d["query_id"]             = val->query_id;
        d["similarity_threshold"] = val->similarity_threshold;
        d["min_match_similarity"] = val->min_match_similarity;

        // PCM data — zero-copy with capsule
        pybind11::capsule base(
            new std::shared_ptr<const data_format::voice_query>(val),
            [](void* p) { delete static_cast<std::shared_ptr<const data_format::voice_query>*>(p); });
        d["pcm_data"] = pybind11::array_t<uint8_t>(
            {static_cast<pybind11::ssize_t>(val->pcm_data.size())},
            {static_cast<pybind11::ssize_t>(1)},
            val->pcm_data.data(),
            base);

        return d;
    }
};

// -----------------------------------------------------------------------
// QueryResponseWriter
// -----------------------------------------------------------------------

struct py_query_response_writer {
    switchboard::writer<data_format::query_response>* writer_;

    /*!
     * @brief Write a query response to the switchboard.
     * @param query_id        Echoed from the originating voice_query.
     * @param point_clouds    List of dicts, each with "points" and "centroid".
     * @param colors          List of per-point-cloud color floats.
     * @param server_latency  Server processing latency in seconds.
     * @param text_query      Original query text echoed back.
     */
    void put(uint64_t         query_id,
             pybind11::list         point_clouds,
             pybind11::list         colors,
             float            server_latency,
             const std::string& text_query) {
        std::shared_ptr<data_format::query_response> resp;
        resp->query_id               = query_id;
        resp->server_query_processing = server_latency;
        resp->text_query             = text_query;
        resp->num_point_clouds       = static_cast<int32_t>(point_clouds.size());

        resp->point_clouds.reserve(point_clouds.size());
        for (auto& item : point_clouds) {
            pybind11::dict pc_dict = item.cast<pybind11::dict>();
            data_format::point_cloud pc;

            auto points_list = pc_dict["points"].cast<std::vector<float>>();
            pc.points        = std::move(points_list);
            pc.num_points    = static_cast<int32_t>(pc.points.size() / 3);

            auto centroid_list = pc_dict["centroid"].cast<std::vector<float>>();
            pc.centroid        = std::move(centroid_list);

            resp->point_clouds.push_back(std::move(pc));
        }

        resp->colors = colors.cast<std::vector<float>>();

        writer_->put(std::move(resp));
    }
};

inline void register_bindings(pybind11::module_& m) {
    pybind11::class_<py_semantic_data_reader>(m, "DnnInputReader").def("get", &py_semantic_data_reader::get);

    pybind11::class_<py_voice_query_reader>(m, "VoiceQueryReader")
        .def("get", &py_voice_query_reader::get,
             "Return the latest voice query as a dict, or None if no new query.");

    pybind11::class_<py_query_response_writer>(m, "QueryResponseWriter")
        .def("put", &py_query_response_writer::put,
             pybind11::arg("query_id"),
             pybind11::arg("point_clouds"),
             pybind11::arg("colors"),
             pybind11::arg("server_latency"),
             pybind11::arg("text_query"),
             "Write a query response to the switchboard.");
}
} // namespace ILLIXR
