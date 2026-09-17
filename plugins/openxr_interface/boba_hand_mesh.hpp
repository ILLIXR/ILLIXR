#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <memory>
#include <numeric>
#include <openxr/openxr.h>
#include <vector>

namespace ILLIXR::boba {

// Local rendering data only: meshes and joint arrays never enter Boba's wire
// protocol. The existing OpenXR trackers own all tracking/gesture decisions.
constexpr std::size_t max_hand_mesh_vertices = 8192;
constexpr std::size_t max_hand_mesh_indices  = 49152;

struct hand_mesh {
    std::array<Eigen::Affine3f, XR_HAND_JOINT_COUNT_EXT> inverse_bind;
    std::vector<Eigen::Vector3f>                         positions;
    std::vector<Eigen::Vector3f>                         normals;
    std::vector<std::array<int, 4>>                      joints;
    std::vector<std::array<float, 4>>                    weights;
    std::vector<std::uint16_t>                           indices;
};

struct hand_mesh_pose {
    std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints{};
    float                                                       scale  = 1.0F;
    bool                                                        active = false;
};

struct hand_mesh_frame {
    std::array<hand_mesh_pose, 2> hands{};
    std::array<XrQuaternionf, 2>  eye_orientations{{{0, 0, 0, 1}, {0, 0, 0, 1}}};
};

inline Eigen::Vector3f vector(const XrVector3f& v) {
    return {v.x, v.y, v.z};
}

inline Eigen::Quaternionf quaternion(const XrQuaternionf& q) {
    return {q.w, q.x, q.y, q.z};
}

inline bool valid_pose(const XrPosef& pose) {
    const auto q = quaternion(pose.orientation);
    return vector(pose.position).allFinite() && q.coeffs().allFinite() && std::abs(q.squaredNorm() - 1.0F) < 0.01F;
}

inline Eigen::Affine3f transform(const XrPosef& pose) {
    return Eigen::Translation3f{vector(pose.position)} * quaternion(pose.orientation).normalized();
}

/** Retrieve immutable bind geometry once from the runtime, using its existing tracker. */
inline std::shared_ptr<const hand_mesh> load_hand_mesh(PFN_xrGetHandMeshFB get_mesh, XrHandTrackerEXT tracker) {
    if (!get_mesh || tracker == XR_NULL_HANDLE) {
        return {};
    }
    XrHandTrackingMeshFB raw{XR_TYPE_HAND_TRACKING_MESH_FB};
    if (XR_FAILED(get_mesh(tracker, &raw)) || raw.jointCountOutput != XR_HAND_JOINT_COUNT_EXT || raw.vertexCountOutput == 0 ||
        raw.vertexCountOutput > max_hand_mesh_vertices || raw.indexCountOutput == 0 ||
        raw.indexCountOutput > max_hand_mesh_indices || raw.indexCountOutput % 3 != 0) {
        return {};
    }
    std::array<XrPosef, XR_HAND_JOINT_COUNT_EXT>        bind{};
    std::array<float, XR_HAND_JOINT_COUNT_EXT>          radii{};
    std::array<XrHandJointEXT, XR_HAND_JOINT_COUNT_EXT> parents{};
    std::vector<XrVector3f>                             positions(raw.vertexCountOutput), normals(raw.vertexCountOutput);
    std::vector<XrVector2f>                             uvs(raw.vertexCountOutput);
    std::vector<XrVector4sFB>                           joints(raw.vertexCountOutput);
    std::vector<XrVector4f>                             weights(raw.vertexCountOutput);
    std::vector<std::int16_t>                           indices(raw.indexCountOutput);
    raw.jointCapacityInput  = bind.size();
    raw.jointBindPoses      = bind.data();
    raw.jointRadii          = radii.data();
    raw.jointParents        = parents.data();
    raw.vertexCapacityInput = positions.size();
    raw.vertexPositions     = positions.data();
    raw.vertexNormals       = normals.data();
    raw.vertexUVs           = uvs.data();
    raw.vertexBlendIndices  = joints.data();
    raw.vertexBlendWeights  = weights.data();
    raw.indexCapacityInput  = indices.size();
    raw.indices             = indices.data();
    if (XR_FAILED(get_mesh(tracker, &raw)) || raw.jointCountOutput != bind.size() ||
        raw.vertexCountOutput != positions.size() || raw.indexCountOutput != indices.size()) {
        return {};
    }
    auto mesh = std::make_shared<hand_mesh>();
    for (std::size_t j = 0; j < bind.size(); ++j) {
        if (!valid_pose(bind[j])) {
            return {};
        }
        mesh->inverse_bind[j] = transform(bind[j]).inverse();
    }
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const std::array<int, 4> bones{joints[i].x, joints[i].y, joints[i].z, joints[i].w};
        std::array<float, 4>     blend{weights[i].x, weights[i].y, weights[i].z, weights[i].w};
        float                    total = 0;
        for (std::size_t b = 0; b < 4; ++b) {
            if (!std::isfinite(blend[b]) || blend[b] < 0 ||
                (blend[b] > 0 && (bones[b] < 0 || bones[b] >= XR_HAND_JOINT_COUNT_EXT))) {
                return {};
            }
            total += blend[b];
        }
        if (!vector(positions[i]).allFinite() || !vector(normals[i]).allFinite() ||
            vector(normals[i]).squaredNorm() < 1.0e-8F || !std::isfinite(total) || total < 1.0e-6F) {
            return {};
        }
        for (auto& weight : blend) {
            weight /= total;
        }
        mesh->positions.push_back(vector(positions[i]));
        mesh->normals.push_back(vector(normals[i]).normalized());
        mesh->joints.push_back(bones);
        mesh->weights.push_back(blend);
    }
    for (const auto index : indices) {
        if (index < 0 || static_cast<std::size_t>(index) >= positions.size()) {
            return {};
        }
        mesh->indices.push_back(static_cast<std::uint16_t>(index));
    }
    return mesh;
}

struct skinned_hand {
    // Positions relative to the CURRENT index tip, in the OpenXR local axes.
    std::vector<Eigen::Vector3f> positions;
    std::vector<Eigen::Vector3f> normals;
};

inline bool hand_present(const hand_mesh_pose& pose) {
    constexpr auto tracked = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
        XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    return pose.active && (pose.joints[XR_HAND_JOINT_WRIST_EXT].locationFlags & tracked) == tracked;
}

/** Linear blend skinning with the runtime's effective scale, without changing its joint output. */
inline bool skin_hand(const hand_mesh& mesh, const hand_mesh_pose& pose, skinned_hand& out) {
    out.positions.clear();
    out.normals.clear();
    if (!hand_present(pose) || !std::isfinite(pose.scale) || pose.scale < 0.25F || pose.scale > 4.0F) {
        return false;
    }
    std::array<Eigen::Affine3f, XR_HAND_JOINT_COUNT_EXT> skin;
    std::array<Eigen::Matrix3f, XR_HAND_JOINT_COUNT_EXT> rotations;
    constexpr auto valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    for (std::size_t j = 0; j < skin.size(); ++j) {
        if ((pose.joints[j].locationFlags & valid) != valid || !valid_pose(pose.joints[j].pose)) {
            return false;
        }
        const auto live = transform(pose.joints[j].pose);
        skin[j]         = live * Eigen::Scaling(pose.scale) * mesh.inverse_bind[j];
        rotations[j]    = live.linear() * mesh.inverse_bind[j].linear();
    }
    const auto tip = vector(pose.joints[XR_HAND_JOINT_INDEX_TIP_EXT].pose.position);
    out.positions.reserve(mesh.positions.size());
    out.normals.reserve(mesh.positions.size());
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        Eigen::Vector3f position = Eigen::Vector3f::Zero(), normal = Eigen::Vector3f::Zero();
        for (std::size_t b = 0; b < 4; ++b) {
            const float weight = mesh.weights[i][b];
            if (weight > 0) {
                const int j = mesh.joints[i][b];
                position += weight * (skin[j] * mesh.positions[i]);
                normal += weight * (rotations[j] * mesh.normals[i]);
            }
        }
        out.positions.push_back(position - tip);
        out.normals.push_back(normal.normalized());
    }
    return true;
}

// Shared with the existing Vulkan overlay pipeline, which consumes source pixels.
struct hand_mesh_vertex {
    float x, y, red, green, blue, alpha;
};

/** Command 2 precedes a hand-only fallback group in the existing 14-float stream. */
inline bool valid_hand_cursor_command(const float* command, std::size_t following_commands) {
    return std::all_of(command, command + 14,
                       [](float v) {
                           return std::isfinite(v);
                       }) &&
        command[0] == 2 && (command[3] == 0 || command[3] == 1) && command[4] >= 1 && command[4] <= 96 &&
        command[4] == std::floor(command[4]) && command[4] <= following_commands && command[5] > 0 && command[5] <= 256 &&
        (command[10] == 0 || command[10] == 1);
}

/** Project an articulated miniature at the cursor; never use the physical hand's translation. */
inline std::vector<hand_mesh_vertex> hand_cursor_triangles(const hand_mesh& mesh, const skinned_hand& hand,
                                                           const XrQuaternionf& eye_orientation, float x, float y, float size,
                                                           const Eigen::Vector3f& color) {
    if (hand.positions.size() != mesh.positions.size() || hand.normals.size() != mesh.positions.size() ||
        !valid_pose({eye_orientation, {0, 0, 0}}) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size) ||
        size <= 0 || size > 256 || !color.allFinite()) {
        return {};
    }
    const auto                   view_rotation = quaternion(eye_orientation).normalized().conjugate();
    std::vector<Eigen::Vector3f> points, colors;
    points.reserve(hand.positions.size());
    colors.reserve(hand.positions.size());
    const Eigen::Vector3f light = Eigen::Vector3f{-0.3F, 0.5F, 1.0F}.normalized();
    for (std::size_t i = 0; i < hand.positions.size(); ++i) {
        // Fixed display scale avoids size pumping as fingers curl. Head-relative
        // orientation and finger articulation are preserved; index tip is (x,y).
        points.push_back(view_rotation * hand.positions[i] * (size / 0.20F));
        const auto  normal = view_rotation * hand.normals[i];
        const float shade  = 0.38F + 0.62F * std::max(0.0F, normal.dot(light));
        colors.push_back(color.cwiseMax(0).cwiseMin(255) * (shade / 255.0F));
    }
    std::vector<std::size_t> order(mesh.indices.size() / 3);
    std::iota(order.begin(), order.end(), 0);
    const auto depth = [&](std::size_t t) {
        return points[mesh.indices[t * 3]].z() + points[mesh.indices[t * 3 + 1]].z() + points[mesh.indices[t * 3 + 2]].z();
    };
    // This small overlay uses the existing color pipeline. Paint far surfaces
    // first so fingers occlude the palm without adding a scene depth buffer.
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return depth(a) < depth(b);
    });
    std::vector<hand_mesh_vertex> triangles;
    triangles.reserve(mesh.indices.size());
    for (const auto t : order) {
        for (std::size_t c = 0; c < 3; ++c) {
            const auto i = mesh.indices[t * 3 + c];
            triangles.push_back({x + points[i].x(), y - points[i].y(), colors[i].x(), colors[i].y(), colors[i].z(), 1.0F});
        }
    }
    return triangles;
}

} // namespace ILLIXR::boba
