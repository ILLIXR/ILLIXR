# module to build and install the realsense ILLIXR plugin

set(PLUGIN_NAME plugin.realsense${ILLIXR_BUILD_SUFFIX})

find_package(realsense2 REQUIRED)
find_package(realsense2-gl REQUIRED)

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        )

# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS} ${Eigen3_INCLUDE_DIRS} ${realsense2_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR}/include)
add_definitions(-Wno-format-extra-args)
target_link_libraries(${PLUGIN_NAME} ${DEPENDENCIES} ${OpenCV_LIBRARIES} ${realsense2_LIBRARY} ${Eigen3_LIBRATIES})
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
