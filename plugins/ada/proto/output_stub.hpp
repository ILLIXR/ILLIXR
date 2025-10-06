#pragma once
/**
 * This file exists purely to suppress IDE errors
 */
#error "Missing generated header vio_input.pb.h"

#include <cstdint>
#include <string>

namespace sr_output_proto {
class Pose {
public:
    [[maybe_unused]] void set_p_x(double) { }

    [[maybe_unused]] void set_p_y(double) { }

    [[maybe_unused]] void set_p_z(double) { }

    [[maybe_unused]] void set_o_x(double) { }

    [[maybe_unused]] void set_o_y(double) { }

    [[maybe_unused]] void set_o_z(double) { }

    [[maybe_unused]] void set_o_w(double) { }
};

class ImgData {
public:
    [[maybe_unused]] void set_img_data(void*, double) { }

    [[maybe_unused]] std::string img_data() const;

    [[maybe_unused]] void set_rows(int) { }

    [[maybe_unused]] void set_columns(int) { }
};

// uncompressed mesh
class MeshData {
public:
    [[maybe_unused]] double v_x(int) const;

    [[maybe_unused]] double v_y(int) const;

    [[maybe_unused]] void set_v_y(double) { }

    [[maybe_unused]] double v_f(int) const;

    [[maybe_unused]] void set_v_f(double) { }

    [[maybe_unused]] int32_t c_r(int) const;

    [[maybe_unused]] void set_c_r(int32_t) { }

    [[maybe_unused]] int32_t c_g(int) const;

    [[maybe_unused]] void set_c_g(int32_t) { }

    [[maybe_unused]] int32_t c_b(int) const;

    [[maybe_unused]] void set_c_b(int32_t) { }

    [[maybe_unused]] int64_t f_1(int) const;

    [[maybe_unused]] void set_f_1(int64_t) { }

    [[maybe_unused]] int64_t f_2(int) const;

    [[maybe_unused]] void set_f_2(int64_t) { }

    [[maybe_unused]] int64_t f_3(int) const;

    [[maybe_unused]] void set_f_3(int64_t) { }
};

// for sending VB lists
class VB {
public:
    [[maybe_unused]] void set_x(int) { }

    [[maybe_unused]] void set_y(int) { }

    [[maybe_unused]] void set_z(int) { }

    [[maybe_unused]] int x() const;

    [[maybe_unused]] int y() const;

    [[maybe_unused]] int z() const;
};

// compressed mesh
// 1 active, 2 inactive, 3 vb_list
class CompressMeshData {
public:
    [[maybe_unused]] void set_draco_data(void*, double) { }

    [[maybe_unused]] std::string draco_data() const;

    [[maybe_unused]] VB* add_vbs();

    [[maybe_unused]] void set_active(unsigned) { }

    [[maybe_unused]] void set_request_id(uint32_t) { }

    [[maybe_unused]] void set_chunk_id(uint32_t) { }

    [[maybe_unused]] void set_max_chunk(uint32_t) { }

    [[maybe_unused]] std::vector<VB> vbs() const;

    [[maybe_unused]] void set_vbs(VB) { }

    [[maybe_unused]] size_t ByteSizeLong() const;

    [[maybe_unused]] void SerializeToArray(void*, int) { }

    [[maybe_unused]] void set_allocated_draco_data(void*) { }

    [[maybe_unused]] bool ParseFromString(const std::string&);

    [[maybe_unused]] unsigned active() const;

    [[maybe_unused]] uint32_t request_id() const;

    [[maybe_unused]] uint32_t chunk_id() const;

    [[maybe_unused]] uint32_t max_chunk() const;
};

} // namespace sr_output_proto
