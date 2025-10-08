#pragma once
/**
 * This file exists purely to suppress IDE errors
 */
#error "Missing generated header vio_input.pb.h"

#include <cstdint>
#include <string>

namespace sr_input_proto {

class Pose {
public:
    [[maybe_unused]] void set_p_x(double) { }

    [[maybe_unused]] void set_p_y(double) { }

    [[maybe_unused]] void set_p_z(double) { }

    [[maybe_unused]] void set_o_x(double) { }

    [[maybe_unused]] void set_o_y(double) { }

    [[maybe_unused]] void set_o_z(double) { }

    [[maybe_unused]] void set_o_w(double) { }

    [[maybe_unused]] double p_x() const;

    [[maybe_unused]] double p_y() const;

    [[maybe_unused]] double p_z() const;

    [[maybe_unused]] double o_x() const;

    [[maybe_unused]] double o_y() const;

    [[maybe_unused]] double o_z() const;

    [[maybe_unused]] double o_w() const;
};

class ImgData {
public:
    [[maybe_unused]] void set_img_data(void*, double) { }

    [[maybe_unused]] const std::string& img_data() const;

    [[maybe_unused]] size_t size() const;

    [[maybe_unused]] void set_rows(int32_t) { }

    [[maybe_unused]] void set_columns(int32_t) { }

    [[maybe_unused]] void set_size(int64_t) { }

    [[maybe_unused]] ImgData* mutable_img_data() const;

    [[maybe_unused]] void swap(const std::string&);

    [[maybe_unused]] size_t rows() const;

    [[maybe_unused]] size_t columns() const;
};

// uncompressed mesh
class MeshData {
public:
    double v_x(int) const;

    [[maybe_unused]] void set_v_x(double) { }

    double v_y(int) const;

    [[maybe_unused]] void set_v_y(double) { }

    double v_f(int) const;

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

// compressed mesh
class CompressMeshData {
public:
    [[maybe_unused]] void set_draco_data(void*, double) { }

    [[maybe_unused]] std::string draco_data() const;
};

class SRSendData {
public:
    [[maybe_unused]] void set_input_pose(Pose) { }

    [[maybe_unused]] void set_depth_img_MSB_data(ImgData) { }

    [[maybe_unused]] void set_depth_img_LSB_data(ImgData) { }

    [[maybe_unused]] void set_rgb_img_data(ImgData) { }

    [[maybe_unused]] void set_id(int32_t) { }

    [[maybe_unused]] bool ParseFromString(std::string);

    [[maybe_unused]] Pose input_pose() const;

    [[maybe_unused]] ImgData depth_img_msb_data() const;

    [[maybe_unused]] ImgData depth_img_lsb_data() const;

    [[maybe_unused]] Pose* mutable_input_pose() const;

    [[maybe_unused]] ImgData* mutable_depth_img_msb_data() const;

    [[maybe_unused]] ImgData* mutable_depth_img_lsb_data() const;

    [[maybe_unused]] size_t ByteSizeLong() const;

    [[maybe_unused]] void SerializeToArray(char*, int) const { }

    [[maybe_unused]] std::string SerializeAsString() const;

    [[maybe_unused]] void set_zmin(double) { }

    [[maybe_unused]] void set_zmax(double) { }

    [[maybe_unused]] double zmin() const;

    [[maybe_unused]] double zmax() const;
};

} // namespace sr_input_proto
