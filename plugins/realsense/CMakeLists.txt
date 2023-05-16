set(PLUGIN_NAME plugin.realsense${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()
include_directories(${OpenCV_INCLUDE_DIRS} ${Eigen3_INCLUDE_DIRS} ${realsense2_INCLUDE_DIR})

add_definitions(-Werror -Wno-format-extra-args)
target_link_libraries(${PLUGIN_NAME} ${DEPENDENCIES} ${OpenCV_LIBRARIES} ${realsense2_LIBRARY} ${Eigen3_LIBRATIES})

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
