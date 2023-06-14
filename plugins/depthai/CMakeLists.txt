# module to build and install the depthai ILLIXR plugin

get_external_for_plugin(DepthAI)

set(PLUGIN_NAME plugin.depthai${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/relative_clock.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        )
add_definitions(-Wno-format-extra-args)

# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${DepthAI_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

# if we are building DepthAI from source
if(DepthAI_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} DepthAI_ext)
    target_link_libraries(${PLUGIN_NAME} PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE ${DepthAI_LIBRARIES})
else()  # DepthAI already installed
    target_link_libraries(${PLUGIN_NAME} PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE depthai::core)
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
