# CMake file for building offload_data plugin for ILLIXR
# Required packages:
#   boost (development)
#   OpenCV including the viz module  (development)

if(NOT ${Boost_FOUND})
    find_package(Boost REQUIRED)
endif()
add_library(plugin.offload_data${ILLIXR_BUILD_SUFFIX} SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(plugin.offload_data${ILLIXR_BUILD_SUFFIX} OpenCV_Viz)
endif()
target_include_directories(plugin.offload_data${ILLIXR_BUILD_SUFFIX} PRIVATE ${Boost_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.offload_data${ILLIXR_BUILD_SUFFIX} boost_filesystem ${OpenCV_LIBRARIES})
target_compile_features(plugin.offload_data${ILLIXR_BUILD_SUFFIX} PRIVATE cxx_std_17)

install(TARGETS plugin.offload_data${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
