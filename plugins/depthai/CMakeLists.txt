set(PLUGIN_NAME plugin.depthai${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()
if(DepthAI_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} DepthAI_ext)
endif()
include_directories(${PLUGIN_NAME} ${DepthAI_INCLUDE_DIRS})

add_definitions(-Werror -Wno-format-extra-args)
target_link_libraries(${PLUGIN_NAME} PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE ${DepthAI_LIBRARIES})
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
