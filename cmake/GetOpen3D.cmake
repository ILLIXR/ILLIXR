# Ada uses the selective extraction API from the separate Open3D-Ada checkout.
# Open3D_DIR may point to its build tree or installed lib/cmake/Open3D directory.
find_package(Open3D 0.19.0 EXACT CONFIG REQUIRED)
include(CMakePushCheckState)
include(CheckCXXSourceCompiles)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES Open3D::Open3D)
unset(ADA_OPEN3D_SELECTIVE_EXTRACTION CACHE)
check_cxx_source_compiles("#include <open3d/t/geometry/VoxelBlockGrid.h>
int main() {
    auto method = &open3d::t::geometry::VoxelBlockGrid::ExtractTriangleMeshForBlocks;
    (void)method;
    return 0;
}" ADA_OPEN3D_SELECTIVE_EXTRACTION)
cmake_pop_check_state()
if(NOT ADA_OPEN3D_SELECTIVE_EXTRACTION)
    message(FATAL_ERROR "ada.open3d requires Open3D-Ada with ExtractTriangleMeshForBlocks. Set Open3D_DIR to the custom CUDA build; see the Ada setup guide.")
endif()
