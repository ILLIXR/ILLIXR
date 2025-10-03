#include "plugin.hpp"

#include "illixr/concurrentqueue/readwritequeue/readerwritercircularbuffer.h"

#include <mutex>
#include <queue>
#include <spdlog/spdlog.h>
#include <thread>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

using b_queue = moodycamel::BlockingReaderWriterCircularBuffer<std::shared_ptr<const mesh_type>>;

std::vector<b_queue> queue_;
std::mutex           writer_mutex_;
std::atomic<bool> done_{false};
std::string          data_path_;

unsigned hash_vb(const ILLIXR::VoxelBlockIndex& index) {
    int x, y, z;
    std::tie(x, y, z) = index;
    auto hash         = (x * 73856093) ^ (y * 19349669) ^ (z * 83492791);
    return static_cast<unsigned>(std::abs(hash) % 25600);
}

void decompress(const uint idx, std::shared_ptr<switchboard::writer<draco_type>> writer) {
    std::shared_ptr<const mesh_type> datum;

    std::fstream decoding_latency;

    // pyh: prepare output directory & open latency log
    decoding_latency.open(data_path_ + "/decoding_latency_" + std::to_string(idx) + ".csv", std::ios::out);
    if (!decoding_latency.is_open()) {
        spdlog::get("illixr")->error("Failed to open decompression latency file {}", data_path_ + "/decoding_latency_" + std::to_string(idx) + ".csv");
    }
    while (true) {
        if (queue_[idx].wait_dequeue_timed(datum, std::chrono::milliseconds(2))) {
            auto start = std::chrono::high_resolution_clock::now();

            draco_illixr::DecoderBuffer buffer;
            buffer.Init(datum->mesh.data(), datum->mesh.size());

            draco_illixr::Decoder               decoder;
            std::unique_ptr<draco_illixr::Mesh> dracoMesh;

            auto                                    type_statusor = draco_illixr::Decoder::GetEncodedGeometryType(&buffer);
            const draco_illixr::EncodedGeometryType geom_type     = type_statusor.value();

            if (geom_type == draco_illixr::TRIANGULAR_MESH) {
                auto statusor = decoder.DecodeMeshFromBuffer(&buffer);
                dracoMesh     = std::move(statusor).value();
            }

            auto       decoding_done = std::chrono::high_resolution_clock::now();
            uint       m_type        = datum->type;
            const auto decoding_us   = std::chrono::duration_cast<std::chrono::microseconds>(decoding_done - start).count();
            decoding_latency << "Decode " << datum->id << " " << datum->chunk_id << " " << (decoding_us / 1000.0) << "\n";

            // pyh: formatting the decoded mesh into Live Mesh Format
            std::unordered_map<unsigned, std::vector<NewVB>> AllocateNewVB;
            AllocateNewVB.reserve(256);

            const draco_illixr::PointAttribute* pos_attribute =
                dracoMesh->GetNamedAttribute(draco_illixr::GeometryAttribute::POSITION);

            if (!pos_attribute) {
                spdlog::get("illixr")->error("No position attribute found in the draco_illixr mesh.");
                return;
            }

            // pyh get voxel block info attached to each face (see section 4.2)
            const int vb_id = dracoMesh->GetAttributeIdByMetadataEntry("attribute_name", "_VOXELBLOCK_INFO");
            auto      vb    = dracoMesh->GetAttributeByUniqueId(vb_id);

            printf("Decompressing chunk %u with %zu faces\n", datum->chunk_id, dracoMesh->num_faces());
            for (draco_illixr::FaceIndex faceIndex(0); faceIndex < dracoMesh->num_faces(); ++faceIndex) {
                float dracoVertex_v1[3], dracoVertex_v2[3], dracoVertex_v3[3];
                int   vb_index_v1[3];

                auto face = dracoMesh->face(faceIndex).data();
                auto v1   = draco_illixr::PointIndex(face[0].value());
                auto v2   = draco_illixr::PointIndex(face[1].value());
                auto v3   = draco_illixr::PointIndex(face[2].value());

                pos_attribute->GetMappedValue(v1, dracoVertex_v1);
                pos_attribute->GetMappedValue(v2, dracoVertex_v2);
                pos_attribute->GetMappedValue(v3, dracoVertex_v3);

                vb->GetMappedValue(v1, vb_index_v1);

                VoxelBlockIndex vb_index{vb_index_v1[0], vb_index_v1[1], vb_index_v1[2]};
                unsigned        hash_idx = hash_vb(vb_index);

                Eigen::Vector3d vertex1(dracoVertex_v1[0], dracoVertex_v1[1], dracoVertex_v1[2]);
                Eigen::Vector3d vertex2(dracoVertex_v2[0], dracoVertex_v2[1], dracoVertex_v2[2]);
                Eigen::Vector3d vertex3(dracoVertex_v3[0], dracoVertex_v3[1], dracoVertex_v3[2]);

                auto& bucketlist = AllocateNewVB[hash_idx];

                bool appended = false;
                for (auto& vb_entry : bucketlist) {
                    if (std::get<0>(vb_entry) == vb_index) {
                        // pyh find existing entry and append vertices
                        std::get<1>(vb_entry).push_back(vertex1);
                        std::get<1>(vb_entry).push_back(vertex2);
                        std::get<1>(vb_entry).push_back(vertex3);
                        appended = true;
                        break;
                    }
                }
                if (!appended) {
                    // pyh means 2 scenarios:
                    // 1. there is a hash collision 2. nothing has been allocated for this VB
                    // either case we need to create a new VB entry
                    std::vector<Eigen::Vector3d> new_vertices;
                    std::vector<Eigen::Vector3d> new_colors; // pyh kept for color case
                    // 8x8x8 vb x 3 faces (avg 2.8 faces in MC possibilities)  * 3 point each
                    new_vertices.reserve(4608);

                    new_vertices.push_back(vertex1);
                    new_vertices.push_back(vertex2);
                    new_vertices.push_back(vertex3);

                    bucketlist.emplace_back(vb_index, std::move(new_vertices), std::move(new_colors));
                }
            }
            {
                std::lock_guard<std::mutex> lock(writer_mutex_);

                writer->put(writer->allocate<draco_type>(draco_type{datum->id, datum->chunk_id, std::move(AllocateNewVB)}));
            }
            auto end       = std::chrono::high_resolution_clock::now();
            auto pvbgen_us = std::chrono::duration_cast<std::chrono::microseconds>(end - decoding_done).count();
            decoding_latency << "PVBGen " << datum->id << " " << (pvbgen_us / 1000.0) << "\n";
            decoding_latency.flush();
        }
        if (done_) {
            break;
        }
    }
}

[[maybe_unused]] mesh_decompression::mesh_decompression(const std::string& name_, ILLIXR::phonebook* pb_)
    : plugin{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , decoded_mesh_{
          std::make_shared<switchboard::writer<draco_type>>(switchboard_->get_writer<draco_type>("decoded_inactive_scene"))} {

    draco_illixr::FileWriterFactory::RegisterWriter(draco_illixr::StdioFileWriter::Open);

    data_path_ = std::filesystem::current_path().string() + "/recorded_data";
    if (!std::filesystem::exists(data_path_)) {
        if (!std::filesystem::create_directories(data_path_)) {
            spdlog::get("illixr")->error("Failed to create data directory.");
        }
    }
    spdlog::get("illixr")->debug("[md] {}", data_path_);
    mesh_count_ = switchboard_->get_env_ulong("MESH_DECOMPRESS_PARALLELISM", 8);

    for (uint i = 0; i < mesh_count_; i++) {
        queue_.push_back(b_queue(8));
        decompress_thread_.push_back(std::thread(decompress, i, decoded_mesh_));
    }
    switchboard_->schedule<mesh_type>(id_, "compressed_scene", [&](switchboard::ptr<const mesh_type> datum, std::size_t) {
        this->process_frame(datum);
    });
}

void mesh_decompression::process_frame(switchboard::ptr<const mesh_type> datum) {
    while (!queue_[datum->type].try_enqueue(datum)) { }
}

mesh_decompression::~mesh_decompression() {
    {
        std::lock_guard<std::mutex> lock(writer_mutex_);
        done_ = true;
    }
    for (auto& t : decompress_thread_) {
        t.join();
    }
}

PLUGIN_MAIN(mesh_decompression)
