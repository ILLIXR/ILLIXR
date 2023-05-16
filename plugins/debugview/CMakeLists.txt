
file(GLOB_RECURSE DEBUGVIEW_SOURCES "./*.cpp")

set(PLUGIN_NAME plugin.debugview${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED ${DEBUGVIEW_SOURCES})

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS} ${glfw3_INCLUDE_DIRS} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR} ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${OpenCV_LIBRARIES} ${glfw3_LIBRARIES} ${gl_LIBRARIES} ${Eigen3_LIBRARIES} dl pthread)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
