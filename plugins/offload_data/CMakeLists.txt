# CMake file for building offload_data plugin for ILLIXR
# Required packages:
#   boost (development)
#   OpenCV including the viz module  (development)

if(NOT ${Boost_FOUND})
    find_package(Boost REQUIRED)
endif()
add_library(plugin.offload_data SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(plugin.offload_data OpenCV_Viz)
endif()
target_include_directories(plugin.offload_data PRIVATE ${Boost_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.offload_data boost_filesystem ${OpenCV_LIBRARIES})
target_compile_features(plugin.offload_data PRIVATE cxx_std_17)

install(TARGETS plugin.offload_data DESTINATION lib)
