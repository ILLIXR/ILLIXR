#include "plugin.hpp"

#include "illixr/concurrentqueue/readwritequeue/readerwritercircularbuffer.h"
#include "illixr/data_format/formatted_mesh.hpp"
#include "mesh_formatter.hpp"

#include <mutex>
#include <queue>
#include <spdlog/spdlog.h>
#include <thread>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

using b_queue = moodycamel::BlockingReaderWriterCircularBuffer<std::shared_ptr<const mesh_type>>;

std::vector<b_queue> queue_;
std::mutex           writer_mutex_;
std::atomic<bool>    done_{false};
std::string          data_path_;

void decompress(const uint idx, std::shared_ptr<switchboard::writer<draco_type>> writer) {
    std::shared_ptr<const mesh_type> datum;
    mesh_formatter                   formatter;

    std::fstream decoding_latency;

    // pyh: prepare output directory & open latency log
    decoding_latency.open(data_path_ + "/decoding_latency_" + std::to_string(idx) + ".csv", std::ios::out);
    if (!decoding_latency.is_open()) {
        spdlog::get("illixr")->error("Failed to open decompression latency file {}",
                                     data_path_ + "/decoding_latency_" + std::to_string(idx) + ".csv");
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

            // Group each chunk independently and publish it as soon as it is ready.
            spdlog::get("illixr")->info("Decompressing chunk {} with {} faces", datum->chunk_id, dracoMesh->num_faces());
            std::shared_ptr<const scene_update_data> formatted;
            try {
                formatted = formatter.format(*dracoMesh);
            } catch (const std::exception& error) {
                spdlog::get("illixr")->error("Unable to format mesh chunk {}: {}", datum->chunk_id, error.what());
                return;
            }
            {
                std::lock_guard<std::mutex> lock(writer_mutex_);

                std::shared_ptr<draco_type> event =
                    std::make_shared<formatted_mesh_type>(datum->id, datum->chunk_id, std::move(formatted));
                writer->put(std::move(event));
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
