#pragma once

/**
 * This file exists purely to suppress IDE errors
 */
#error "Missing generated header output.pb.h"

#include <string>

namespace unity_pose_proto {
class Position {
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
};

class Quaternion {
public:
    void set_w(double) { }

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

class Pose {
public:
    int timestamp() const {
        return 0;
    }

    Position pos() const {
        return {};
    }

    Quaternion quat() const {
        return {};
    }

    void set_allocated_pos(Position*) { }

    void set_allocated_quat(Quaternion*) { }

    void set_timestamp(long) { }

    bool ParseFromString(std::string) {
        return {};
    }

    std::string SerializeAsString() {
        return "";
    }
};

} // namespace unity_pose_proto
