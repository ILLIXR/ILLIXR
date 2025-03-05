#include "illixr/error_util.hpp"

#include <deque>
#include <eigen3/Eigen/Dense>
#include <fstream>
#include <string>

Eigen::Matrix<float, 3, 3> skew_x(const Eigen::Matrix<float, 3, 1>& w) {
    Eigen::Matrix<float, 3, 3> w_x;
    w_x << 0, -w(2), w(1), w(2), 0, -w(0), -w(1), w(0), 0;
    return w_x;
}

Eigen::Matrix<float, 4, 1> ori_inv(Eigen::Matrix<float, 4, 1> q) {
    Eigen::Matrix<float, 4, 1> qinv;
    qinv.block(0, 0, 3, 1) = -q.block(0, 0, 3, 1);
    qinv(3, 0)             = q(3, 0);
    return qinv;
}

Eigen::Matrix<float, 4, 1> ori_multiply(const Eigen::Matrix<float, 4, 1>& q, const Eigen::Matrix<float, 4, 1>& p) {
    Eigen::Matrix<float, 4, 1> q_t;
    Eigen::Matrix<float, 4, 4> Qm;
    // Create big L matrix
    Qm.block(0, 0, 3, 3) = q(3, 0) * Eigen::MatrixXf::Identity(3, 3) - skew_x(q.block(0, 0, 3, 1));
    Qm.block(0, 3, 3, 1) = q.block(0, 0, 3, 1);
    Qm.block(3, 0, 1, 3) = -q.block(0, 0, 3, 1).transpose();
    Qm(3, 3)             = q(3, 0);
    q_t                  = Qm * p;
    // Ensure unique by forcing q_4 to be >0
    if (q_t(3, 0) < 0) {
        q_t *= -1;
    }
    // Normalize and return
    return q_t / q_t.norm();
}

void read_line(std::string stream, std::deque<float>& parameters) {
    const char*            split = " ";
    std::string::size_type pos   = stream.find(" ");
    stream.erase(0, pos + 1);

    char* token = strtok((char*) stream.c_str(), split);
    while (token != NULL) {
        parameters.push_back(atof(token));
        token = strtok(NULL, split);
    }
}

template<typename EigenType>
void assign_matrix(std::deque<float>& parameters, EigenType& eigen_data) {
    for (int row = 0; row < eigen_data.rows(); row++) {
        for (int col = 0; col < eigen_data.cols(); col++) {
            float value          = parameters[0];
            eigen_data(row, col) = value;
            parameters.pop_front();
        }
    }
}

void load_align_parameters(const std::string& path, Eigen::Matrix3f& align_rot, Eigen::Vector3f& align_trans,
                           Eigen::Vector4f& align_quat, double& align_scale) {
    std::ifstream infile;
    infile.open(path);
    if (!infile.is_open()) {
        ILLIXR::abort("Open alignment file failed !!!");
    }

    std::string       in;
    std::deque<float> parameters;
    while (std::getline(infile, in)) {
        parameters.resize(0);
        read_line(in, parameters);

        if (strstr(in.c_str(), "Rotation")) {
            assign_matrix(parameters, align_rot);
            continue;
        }

        if (strstr(in.c_str(), "Translation")) {
            assign_matrix(parameters, align_trans);
            continue;
        }

        if (strstr(in.c_str(), "Quaternion")) {
            assign_matrix(parameters, align_quat);
            continue;
        }

        if (strstr(in.c_str(), "Scale")) {
            align_scale = parameters[0];
            continue;
        }
    }
    infile.close();
}
