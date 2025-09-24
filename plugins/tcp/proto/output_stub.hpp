#pragma once

/**
 * This file exists purely to suppress IDE errors
 */
#error "Missing generated header output.pb.h"

#include <string>

namespace output_proto {
class Quat {
public:
    void set_w(double) {}

    void set_x(double) { }

    void set_y(double) { }

    void set_z(double) { }

    double w() const {
        return 0;
    }

    double x() const {
        return 0;
    }

    double y() const {
        return 0;
    }

    double z() const {
        return 0;
    }
};

class Rot {
public:
    void set_theta(double) {}

    void set_rho(double) {}

    double theta() const {
        return 0;
    }

    double rho() const {
        return 0;
    }
};

class Movement {
public:
    Rot rotation() const {
        return {};
    }

    Quat quat() const {
        return {};
    }

    void set_allocated_rotation(Rot*) {}

    void set_allocated_quat(Quat*) {}

    bool ParseFromString(std::string) {
        return {};
    }

    std::string SerializeAsString() {
        return "";
    }
};

}
