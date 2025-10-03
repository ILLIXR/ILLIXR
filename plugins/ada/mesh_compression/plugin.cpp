#include "plugin.hpp"

#include "illixr/concurrentqueue/readwritequeue/readerwritercircularbuffer.h"

#include <condition_variable>
#include <draco_illixr/compression/encode.h>
#include <draco_illixr/compression/expert_encode.h>
#include <draco_illixr/io/file_reader_factory.h>
#include <draco_illixr/io/file_writer_factory.h>
#include <draco_illixr/io/ply_decoder.h>
#include <draco_illixr/io/stdio_file_reader.h>
#include <draco_illixr/io/stdio_file_writer.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <spdlog/spdlog.h>
#include <thread>
#include <atomic>

const int tex_coords_quantization_bits_ = 10;
const int normals_quantization_bits_    = 8;
const int generic_quantization_bits_    = 8;
const int compression_level_            = 7;
const int speed_                        = 10 - compression_level_;

using namespace ILLIXR;
using namespace ILLIXR::data_format;

using b_queue = moodycamel::BlockingReaderWriterCircularBuffer<std::shared_ptr<const mesh_type>>;

std::vector<b_queue> queue_;
std::mutex  writer_mutex_;
std::atomic<bool> done_{false};

std::string data_path_;

const float origin[] = {0.0f, 0.0f, 0.0f};
const float range    = 2000.0f;

void compress(const uint idx, std::shared_ptr<switchboard::writer<mesh_type>> writer) {
    std::shared_ptr<const mesh_type> datum;
    draco_illixr::Encoder            encoder_;
    encoder_.SetAttributeExplicitQuantization(draco_illixr::GeometryAttribute::POSITION, 14, 3, origin, range);

    encoder_.SetAttributeQuantization(draco_illixr::GeometryAttribute::TEX_COORD, tex_coords_quantization_bits_);
    encoder_.SetAttributeQuantization(draco_illixr::GeometryAttribute::NORMAL, normals_quantization_bits_);
    encoder_.SetAttributeQuantization(draco_illixr::GeometryAttribute::GENERIC, generic_quantization_bits_);
    encoder_.SetSpeedOptions(speed_, speed_);

    std::fstream compression_latency_;
    compression_latency_.open(data_path_ + "/compression_latency_" + std::to_string(idx) + ".csv", std::ios::out);
    if (!compression_latency_.is_open()) {
        spdlog::get("illixr")->error("Failed to open compression latency file {}", idx);
    }


    while (true) {
        if (queue_[idx].wait_dequeue_timed(datum, std::chrono::milliseconds(2))) {
            auto start = std::chrono::high_resolution_clock::now();

            std::unique_ptr<draco_illixr::PlyDecoder> ply_decoder = std::make_unique<draco_illixr::PlyDecoder>();
            std::unique_ptr<draco_illixr::Mesh>       draco_mesh  = std::make_unique<draco_illixr::Mesh>();

            ply_decoder->out_mesh_        = draco_mesh.get();
            ply_decoder->out_point_cloud_ = static_cast<draco_illixr::PointCloud*>(draco_mesh.get());

            ply_decoder->DecodeExternal(*(datum->reader.get()), false);

            // expert_encoder.reset(new draco_illixr::ExpertEncoder(*(std::move(draco_mesh))));
            // draco_illixr::PointCloud *draco_pc = draco_mesh.get();
            std::unique_ptr<draco_illixr::ExpertEncoder> expert_encoder_ =
                std::make_unique<draco_illixr::ExpertEncoder>(*draco_mesh);
            expert_encoder_->Reset(encoder_.CreateExpertEncoderOptions(*draco_mesh));

            // expert_encoder->Reset(encoder.CreateExpertEncoderOptions(*draco_pc));
            draco_illixr::EncoderBuffer draco_buffer;

            const draco_illixr::Status status   = expert_encoder_->EncodeToBuffer(&draco_buffer);
            auto                       end      = std::chrono::high_resolution_clock::now();
            auto                       duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

            unsigned mesh_t = datum->type;
            compression_latency_ << (duration / 1000.0) << "\n";
            compression_latency_.flush();

            {
                std::lock_guard<std::mutex> lock(writer_mutex_);
                writer->put(writer->allocate<mesh_type>(
                    mesh_type{mesh_t, *(draco_buffer.buffer()), datum->active, datum->id, datum->chunk_id, datum->max_chunk}));
            }
        }
        if (done_) {
            break;
        }
    }
}

[[maybe_unused]] mesh_compression::mesh_compression(const std::string& name_, ILLIXR::phonebook* pb_)
    : plugin{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , compressed_mesh_{std::make_shared<switchboard::writer<data_format::mesh_type>>(
          switchboard_->get_writer<mesh_type>("compressed_scene"))} {
    
    draco_illixr::FileReaderFactory::RegisterReader(draco_illixr::StdioFileReader::Open);
    draco_illixr::FileWriterFactory::RegisterWriter(draco_illixr::StdioFileWriter::Open);
    data_path_ = std::filesystem::current_path().string() + "/recorded_data";
    if (!std::filesystem::exists(data_path_)) {
        if (!std::filesystem::create_directories(data_path_)) {
            spdlog::get("illixr")->error("Failed to create data directory.");
        }
    }
    mesh_count_ = switchboard_->get_env_ulong("MESH_COMPRESS_PARALLELISM", 8);

    for (uint i = 0; i < mesh_count_; i++) {
        queue_.push_back(b_queue(8));
        compress_thread_.push_back(std::thread(compress, i, compressed_mesh_));
    }
    switchboard_->schedule<mesh_type>(id_, "requested_scene", [&](switchboard::ptr<const mesh_type> datum, std::size_t) {
        this->process_mesh(datum);
    });
}

void mesh_compression::process_mesh(switchboard::ptr<const mesh_type> datum) {
    while (!queue_[datum->type].try_enqueue(datum)) { }
}

mesh_compression::~mesh_compression() {
    {
        std::lock_guard<std::mutex> lock(writer_mutex_);
        done_ = true;
    }
    for (auto& t : compress_thread_) {
        t.join();
    }
}

PLUGIN_MAIN(mesh_compression)
