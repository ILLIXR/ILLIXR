#pragma once
/**
 * This file exists purely to suppress IDE errors
 */
#error "Missing generated header rendered_frame.pb.h"

#include <cstdint>
#include <string>

namespace rendered_frame_proto {

class Frame {
public:
    [[maybe_unused]] void set_left_eye(void*, double) { }

    [[maybe_unused]] void set_right_eye(void*, double) { }

    [[maybe_unused]] const std::string& left_eye() const {
        return "";
    }

    [[maybe_unused]] const std::string& right_eye() const {
        return "";
    }

    [[maybe_unused]] void set_rows(int32_t) { }

    [[maybe_unused]] void set_columns(int32_t) { }

    [[maybe_unused]] int32_t rows() const {
        return 0;
    }

    [[maybe_unused]] int32_t columns() const {
        return 0;
    }

    [[maybe_unused]] Frame* mutable_img_data() const {
        Frame frm;
        return &frm;
    }

    [[maybe_unused]] void swap(const std::string&) { }

    [[maybe_unused]] bool ParseFromString(const std::string&) {
        return true;
    }

    [[maybe_unused]] std::string SerializeAsString() const {
        return "";
    }
};

class CompressedFrame {
public:
    [[maybe_unused]] void set_timestamp(int) { }

    [[maybe_unused]] void set_left_eye(void*, double) { }

    [[maybe_unused]] void set_right_eye(void*, double) { }

    [[maybe_unused]] void set_left_eye_size(int) { }

    [[maybe_unused]] void set_right_eye_size(int) { }

    [[maybe_unused]] int timestamp() const {
        return 0;
    }

    [[maybe_unused]] const std::string& left_eye() const {
        return "";
    }

    [[maybe_unused]] const std::string& right_eye() const {
        return "";
    }

    [[maybe_unused]] int32_t left_eye_size() const {
        return 0;
    }

    [[maybe_unused]] int32_t right_eye_size() const {
        return 0;
    }

    [[maybe_unused]] void set_rows(int32_t) { }

    [[maybe_unused]] void set_columns(int32_t) { }

    [[maybe_unused]] int32_t rows() const {
        return 0;
    }

    [[maybe_unused]] int32_t columns() const {
        return 0;
    }

    [[maybe_unused]] Frame* mutable_img_data() const {
        Frame frm;
        return &frm;
    }

    [[maybe_unused]] void swap(const std::string&) { }

    [[maybe_unused]] bool ParseFromString(const std::string&) {
        return true;
    }

    [[maybe_unused]] std::string SerializeAsString() const {
        return "";
    }
};

} // namespace rendered_frame_proto
