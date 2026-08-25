/*!
 * @file
 * @brief  pybind11 bindings exposing ILLIXR switchboard topics to Python.
 *
 *         Three proxy structs are registered as Python classes:
 *
 *           SemanticDataReader  - pairs the most recently decoded RGB frame
 *             (decoded_frame_cache) with the depth/pose/intrinsics metadata
 *             for that same frame_number (semantic_metadata_cache). Decode
 *             lags arrival by a variable amount, so pairing is done by
 *             frame_number rather than by reading the most recently arrived
 *             switchboard sample directly.
 *             .get() -> dict or None (None if no frame has been decoded yet,
 *                                      or its metadata has since been evicted)
 *               "image"           numpy uint8 (H,W,3)   decoded RGB - zero-copy from cache
 *               "depth"           numpy uint8 (N,)      raw R16_UNORM depth bytes
 *               "frame_number"    int
 *               "width"           int                   (from intrinsics.width)
 *               "height"          int                   (from intrinsics.height)
 *               "depth_width"     int                   (from depth_intrinsics.width)
 *               "depth_height"    int                   (from depth_intrinsics.height)
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

#include "plugin.hpp"

#include <cstdint>
#include <memory>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <utility>

namespace ILLIXR {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static pybind11::array_t<uint8_t> to_numpy_flat_safe(std::shared_ptr<const data_format::semantic_data> owner,
                                                     const uint8_t* data, size_t size) {
    pybind11::capsule base(new std::shared_ptr<const data_format::semantic_data>(std::move(owner)), [](void* p) {
        delete static_cast<std::shared_ptr<const data_format::semantic_data>*>(p);
    });

    return pybind11::array_t<uint8_t>({static_cast<pybind11::ssize_t>(size)}, {static_cast<pybind11::ssize_t>(1)}, data, base);
}

static pybind11::array_t<float> to_numpy_4x4_safe(std::shared_ptr<const data_format::semantic_data> owner, const float* data) {
    pybind11::capsule base(new std::shared_ptr<const data_format::semantic_data>(std::move(owner)), [](void* p) {
        delete static_cast<std::shared_ptr<const data_format::semantic_data>*>(p);
    });

    return pybind11::array_t<float>(
        {static_cast<pybind11::ssize_t>(4), static_cast<pybind11::ssize_t>(4)},
        {static_cast<pybind11::ssize_t>(sizeof(float) * 4), static_cast<pybind11::ssize_t>(sizeof(float))}, data, base);
}

// Convert a camera_intrinsics struct to a 1D numpy float32 array [fx, fy, cx, cy].
// Copies the four floats by value — the struct is not contiguous in memory, so
// zero-copy is not applicable here.
static pybind11::array_t<float> intrinsics_to_numpy(const data_format::camera_intrinsics& intr) {
    auto result = pybind11::array_t<float>(4);
    auto buf    = result.mutable_unchecked<1>();
    buf(0)      = intr.fx;
    buf(1)      = intr.fy;
    buf(2)      = intr.cx;
    buf(3)      = intr.cy;
    return result;
}

// Wrap a DecodedFrameCache::Entry's RGB vector as a (H, W, 3) uint8 numpy
// array without copying. The capsule keeps the Entry alive via a shared_ptr
// to the cache entry data. Since Entry is owned by the cache (fixed array),
// we share-own a copy of the rgb vector data instead.
static pybind11::array_t<uint8_t> entry_to_numpy(const decode::decoded_frame_cache::Entry* entry) {
    if (entry == nullptr || entry->rgb.empty())
        return pybind11::array_t<uint8_t>();

    // Copy the vector into a heap-allocated buffer owned by the capsule.
    // This is one memcpy but avoids lifetime issues with the cache slot
    // being overwritten while Python holds a reference.
    auto*             buf = new std::vector<uint8_t>(entry->rgb);
    pybind11::capsule base(buf, [](void* p) {
        delete static_cast<std::vector<uint8_t>*>(p);
    });

    return pybind11::array_t<uint8_t>({static_cast<pybind11::ssize_t>(entry->height),
                                       static_cast<pybind11::ssize_t>(entry->width), static_cast<pybind11::ssize_t>(3)},
                                      {static_cast<pybind11::ssize_t>(entry->width * 3), static_cast<pybind11::ssize_t>(3),
                                       static_cast<pybind11::ssize_t>(1)},
                                      buf->data(), base);
}

// ---------------------------------------------------------------------------
// SemanticDataReader proxy
// ---------------------------------------------------------------------------

struct py_semantic_data_reader {
    decode::decoded_frame_cache*     cache;
    decode::semantic_metadata_cache* metadata_cache;

    [[nodiscard]] pybind11::object get() const {
        // Pull from whichever frame decode has actually finished, rather
        // than the most recently arrived frame — decode lags arrival by a
        // variable amount (see nvdec_decoder::decode). metadata_cache is
        // keyed by the same frame_number the decoder returns (see
        // plugin.cpp on_semantic_data), so looking it up here guarantees
        // the depth/pose returned below belong to the same frame as
        // "image".
        const decode::decoded_frame_cache::Entry* entry = cache ? cache->latest() : nullptr;
        if (entry == nullptr)
            return pybind11::none();

        const decode::semantic_metadata_cache::Entry* meta =
            metadata_cache ? metadata_cache->find(entry->frame_number) : nullptr;
        if (meta == nullptr)
            return pybind11::none();

        auto val = meta->data;

        pybind11::dict data;
        data["image"]            = entry_to_numpy(entry);
        data["frame_number"]     = entry->frame_number;
        data["image_width"]      = val->intrinsics.width;
        data["image_height"]     = val->intrinsics.height;
        data["depth"]            = to_numpy_flat_safe(val, val->depth.data(), val->depth.size());
        data["depth_width"]      = val->depth_intrinsics.width;
        data["depth_height"]     = val->depth_intrinsics.height;
        data["depth_near_z"]     = val->depth_near_z;
        data["intrinsics"]       = intrinsics_to_numpy(val->intrinsics);
        data["depth_intrinsics"] = intrinsics_to_numpy(val->depth_intrinsics);
        data["rgb_camera_pose"]  = to_numpy_4x4_safe(val, val->rgb_camera_pose);
        data["depth_pose"]       = to_numpy_4x4_safe(val, val->depth_pose);
        data["max_depth_m"]      = val->max_depth;

        return data;
    }
};

// -----------------------------------------------------------------------
// VoiceQueryReader
// -----------------------------------------------------------------------

struct py_voice_query_reader {
    switchboard::reader<data_format::voice_query>* reader_;
    uint64_t                                       last_query_id_ = 0;

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
        pybind11::capsule base(new std::shared_ptr<const data_format::voice_query>(val), [](void* p) {
            delete static_cast<std::shared_ptr<const data_format::voice_query>*>(p);
        });
        d["pcm_data"] = pybind11::array_t<uint8_t>({static_cast<pybind11::ssize_t>(val->pcm_data.size())},
                                                   {static_cast<pybind11::ssize_t>(1)}, val->pcm_data.data(), base);

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
    void put(uint64_t query_id, pybind11::list point_clouds, pybind11::list colors, float server_latency,
             const std::string& text_query) const {
        auto resp                     = std::make_shared<data_format::query_response>();
        resp->query_id                = query_id;
        resp->server_query_processing = server_latency;
        resp->text_query              = text_query;
        resp->num_point_clouds        = static_cast<int32_t>(point_clouds.size());

        resp->point_clouds.reserve(point_clouds.size());
        for (auto& item : point_clouds) {
            auto                     pc_dict = item.cast<pybind11::dict>();
            data_format::point_cloud pc;
            pc.points     = pc_dict["points"].cast<std::vector<float>>();
            pc.num_points = static_cast<int32_t>(pc.points.size() / 3);
            pc.centroid   = pc_dict["centroid"].cast<std::vector<float>>();
            resp->point_clouds.push_back(std::move(pc));
        }

        resp->colors = colors.cast<std::vector<float>>();

        writer_->put(std::move(resp));
    }
};

inline void register_bindings(pybind11::module_& m) {
    pybind11::class_<py_semantic_data_reader>(m, "DnnInputReader").def("get", &py_semantic_data_reader::get);

    pybind11::class_<py_voice_query_reader>(m, "VoiceQueryReader")
        .def("get", &py_voice_query_reader::get, "Return the latest voice query as a dict, or None if no new query.");

    pybind11::class_<py_query_response_writer>(m, "QueryResponseWriter")
        .def("put", &py_query_response_writer::put, pybind11::arg("query_id"), pybind11::arg("point_clouds"),
             pybind11::arg("colors"), pybind11::arg("server_latency"), pybind11::arg("text_query"),
             "Write a query response to the switchboard.");
}
} // namespace ILLIXR
