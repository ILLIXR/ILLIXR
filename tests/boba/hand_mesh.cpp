#include "plugins/openxr_interface/boba_hand_mesh.hpp"

#include <cassert>
#include <iostream>
#include <limits>

using namespace ILLIXR::boba;

namespace {
int calls = 0;
enum class fault { none, query, fill, oversized, index, weight, bind, bone };
fault          injected = fault::none;
constexpr auto flags    = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT |
    XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;

XrPosef bind_pose(std::size_t joint) {
    return {{0, 0, 0, 1}, {0.01F * joint, 0.02F * joint, -0.005F * joint}};
}

XrResult XRAPI_PTR get_mesh(XrHandTrackerEXT, XrHandTrackingMeshFB* mesh) {
    ++calls;
    mesh->jointCountOutput  = XR_HAND_JOINT_COUNT_EXT;
    mesh->vertexCountOutput = injected == fault::oversized ? 100000 : 3;
    mesh->indexCountOutput  = 3;
    if (!mesh->vertexCapacityInput) {
        return injected == fault::query ? XR_ERROR_RUNTIME_FAILURE : XR_SUCCESS;
    }
    if (injected == fault::fill) {
        return XR_ERROR_RUNTIME_FAILURE;
    }
    assert(mesh->jointCapacityInput == XR_HAND_JOINT_COUNT_EXT && mesh->vertexCapacityInput == 3 &&
           mesh->indexCapacityInput == 3);
    for (std::size_t j = 0; j < XR_HAND_JOINT_COUNT_EXT; ++j) {
        mesh->jointBindPoses[j] = bind_pose(j);
        mesh->jointRadii[j]     = 0.005F;
        mesh->jointParents[j]   = XR_HAND_JOINT_WRIST_EXT;
    }
    const auto tip = bind_pose(XR_HAND_JOINT_INDEX_TIP_EXT).position;
    for (std::size_t i = 0; i < 3; ++i) {
        mesh->vertexPositions[i]    = {tip.x + (i == 1 ? 0.02F : 0), tip.y + (i == 2 ? 0.02F : 0), tip.z};
        mesh->vertexNormals[i]      = {0, 0, 1};
        mesh->vertexUVs[i]          = {0, 0};
        mesh->vertexBlendIndices[i] = {XR_HAND_JOINT_INDEX_TIP_EXT, -1, -1, -1};
        mesh->vertexBlendWeights[i] = {1, 0, 0, 0};
        mesh->indices[i]            = i;
    }
    if (injected == fault::index)
        mesh->indices[2] = -1;
    if (injected == fault::weight)
        mesh->vertexBlendWeights[0].x = std::numeric_limits<float>::quiet_NaN();
    if (injected == fault::bind)
        mesh->jointBindPoses[0].orientation.w = 0;
    if (injected == fault::bone)
        mesh->vertexBlendIndices[0].x = 26;
    return XR_SUCCESS;
}

hand_mesh_pose tracked_pose() {
    hand_mesh_pose pose;
    pose.active = true;
    for (std::size_t j = 0; j < pose.joints.size(); ++j) {
        pose.joints[j].locationFlags = flags;
        pose.joints[j].pose          = bind_pose(j);
    }
    return pose;
}

bool near(float a, float b) {
    return std::abs(a - b) < 0.0001F;
}
} // namespace

int main() {
    const auto tracker = reinterpret_cast<XrHandTrackerEXT>(std::uintptr_t{1});
    assert(!load_hand_mesh(nullptr, tracker));
    assert(!load_hand_mesh(get_mesh, XR_NULL_HANDLE));
    auto mesh = load_hand_mesh(get_mesh, tracker);
    assert(mesh && calls == 2 && mesh->indices.size() == 3);
    for (auto error : {fault::query, fault::fill, fault::oversized, fault::index, fault::weight, fault::bind, fault::bone}) {
        injected = error;
        assert(!load_hand_mesh(get_mesh, tracker));
    }
    injected = fault::none;

    auto         pose = tracked_pose();
    skinned_hand skinned;
    assert(skin_hand(*mesh, pose, skinned));
    assert(skinned.positions[0].norm() < 0.00001F); // index tip is the cursor origin
    assert(near(skinned.positions[1].x(), 0.02F));
    const auto initial = skinned.positions;
    // Physical hand translation must not pull the mesh off a remote target.
    for (auto& joint : pose.joints) {
        joint.pose.position.x += 0.4F;
        joint.pose.position.y += 0.7F;
        joint.pose.position.z -= 0.3F;
    }
    assert(skin_hand(*mesh, pose, skinned));
    for (std::size_t i = 0; i < initial.size(); ++i)
        assert((skinned.positions[i] - initial[i]).norm() < 0.00001F);
    // Finger articulation changes geometry while the fingertip stays anchored.
    const float s                                             = std::sqrt(0.5F);
    pose.joints[XR_HAND_JOINT_INDEX_TIP_EXT].pose.orientation = {0, 0, s, s};
    assert(skin_hand(*mesh, pose, skinned));
    assert(skinned.positions[0].norm() < 0.00001F);
    assert(near(skinned.positions[1].x(), 0) && near(skinned.positions[1].y(), 0.02F));
    pose.scale = 1.3F;
    assert(skin_hand(*mesh, pose, skinned));
    assert(near(skinned.positions[1].y(), 0.026F));

    // Per-eye cursor locations remain authoritative, even at a held contact/menu.
    for (const auto anchor : {Eigen::Vector2f{350, 220}, Eigen::Vector2f{310, 190}}) {
        const auto triangles = hand_cursor_triangles(*mesh, skinned, {0, 0, 0, 1}, anchor.x(), anchor.y(), 72, {131, 203, 235});
        assert(triangles.size() == 3);
        assert(near(triangles[0].x, anchor.x()) && near(triangles[0].y, anchor.y()));
        assert(near(triangles[1].y, anchor.y() - 0.026F * 360));
        for (const auto& v : triangles)
            assert(std::isfinite(v.x) && std::isfinite(v.y) && v.alpha == 1 && v.blue > v.red);
    }
    const auto head_rotated = hand_cursor_triangles(*mesh, skinned, {0, 0, s, s}, 350, 220, 72, {131, 203, 235});
    assert(near(head_rotated[1].x, 350 + 0.026F * 360) && near(head_rotated[1].y, 220));
    assert(hand_cursor_triangles(*mesh, skinned, {0, 0, 0, 0}, 0, 0, 72, {1, 1, 1}).empty());

    // One absent hand cannot survive through a prior mesh, or affect the other.
    auto left = pose, right = pose;
    left.active = false;
    assert(!skin_hand(*mesh, left, skinned) && skinned.positions.empty());
    assert(skin_hand(*mesh, right, skinned));
    left = pose;
    left.joints[XR_HAND_JOINT_WRIST_EXT].locationFlags &= ~XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    assert(!hand_present(left) && !skin_hand(*mesh, left, skinned));
    left                                                   = pose;
    left.joints[XR_HAND_JOINT_INDEX_TIP_EXT].locationFlags = 0;
    assert(!skin_hand(*mesh, left, skinned) && skinned.positions.empty());
    left       = pose;
    left.scale = std::numeric_limits<float>::quiet_NaN();
    assert(!skin_hand(*mesh, left, skinned));

    float command[14] = {2, 350, 220, 1, 43, 72, 1, 131, 203, 235, 0, 0, 0, 0};
    assert(valid_hand_cursor_command(command, 43));
    assert(!valid_hand_cursor_command(command, 42));
    command[3] = 2;
    assert(!valid_hand_cursor_command(command, 43));
    command[3] = 1;
    command[4] = 42.5F;
    assert(!valid_hand_cursor_command(command, 43));
    command[4] = 43;
    command[1] = std::numeric_limits<float>::infinity();
    assert(!valid_hand_cursor_command(command, 43));
    std::cout << "OpenXR mesh loading, skinning, cursor anchoring, scale, orientation, tracking loss and metadata: PASS\n";
}
