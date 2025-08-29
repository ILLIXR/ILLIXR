#pragma once

#include "illixr/data_format/draco.hpp"
#include "illixr/data_format/mesh.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"

// draco_illixr related libraries
#include "draco_illixr/compression/decode.h"
#include "draco_illixr/io/file_utils.h"
#include "draco_illixr/io/ply_decoder.h"
#include "draco_illixr/io/ply_property_writer.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <draco_illixr/io/file_writer_factory.h>
#include <draco_illixr/io/ply_reader.h>
#include <draco_illixr/io/stdio_file_writer.h>
#include <eigen3/Eigen/Dense>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ILLIXR {

using VoxelBlockIndex = std::tuple<int, int, int>;
using NewVB           = std::tuple<VoxelBlockIndex, std::vector<Eigen::Vector3d>, std::vector<Eigen::Vector3d>>;

class mesh_decompression : public plugin {
public:
    [[maybe_unused]] mesh_decompression(const std::string& name_, phonebook* pb_);

    ~mesh_decompression() override;

    void process_frame(switchboard::ptr<const data_format::mesh_type> datum);

private:
    uint mesh_count_;
    const std::shared_ptr<switchboard>           switchboard_;
    std::shared_ptr<switchboard::writer<data_format::draco_type>> decoded_mesh_;
    std::vector<std::thread> decompress_thread_;

};

} // namespace ILLIXR
