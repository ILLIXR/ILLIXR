cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)

set(PLUGIN_NAME plugin.ada.scene_management${ILLIXR_BUILD_SUFFIX})

# Find OpenMP
find_package(OpenMP REQUIRED)

# Find Draco library
get_external_for_plugin(Draco)

# Add the plugin library
add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
            spatial_hash.cpp
            spatial_hash.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/data_format/draco.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/data_format/mesh.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/data_format/scene_reconstruction.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/relative_clock.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/threadloop.hpp
)

# Link OpenMP
target_link_libraries(${PLUGIN_NAME} OpenMP::OpenMP_CXX draco_illixr::draco ${OpenCV_LIBRARIES})
target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)


if(TARGET draco_static)
    add_dependencies(${PLUGIN_NAME} draco_static)
    target_include_directories(${PLUGIN_NAME} PUBLIC ${draco_illixr_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
endif()


install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
