#pragma once

/**
 * This file exists purely to suppress IDE errors
 */
#error "Missing generated header input.pb.h"

namespace input_proto {
class Vec3 {
public:
    void set_x(double) { }

    void set_y(double) { }

    void set_z(double) { }

    double x() const {
        return 0;
    }

    double y() const {
        return 0;
    }

    double z() const {
        return 0;
    }

    bool ParseFromString(std::string) {
        return {};
    }

    std::string SerializeAsString() {
        return "";
    }
};
}