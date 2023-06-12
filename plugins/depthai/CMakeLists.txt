get_external(DepthAI)

set(PLUGIN_NAME plugin.depthai${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp)
add_definitions(-Wno-format-extra-args)

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${DepthAI_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

if(DepthAI_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} DepthAI_ext)
    target_link_libraries(${PLUGIN_NAME} PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE ${DepthAI_LIBRARIES})
else()
    target_link_libraries(${PLUGIN_NAME} PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE depthai::core)
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
