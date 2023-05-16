if(NOT PkgConfig_FOUND)
    find_package(PkgConfig REQUIRED)
endif()
if(NOT X11_FOUND)
    find_package(X11 REQUIRED)
endif()
if(NOT GLEW_FOUND)
    find_package(GLEW REQUIRED)
endif()
if(NOT glu_FOUND)
    pkg_check_modules(glu REQUIRED glu)
endif()
if(NOT gl_FOUND)
    pkg_check_modules(gl REQUIRED gl)
endif()

find_package(glfw3 REQUIRED)
file(GLOB_RECURSE DEBUGVIEW_SOURCES "./*.cpp")
add_library(plugin.debugview${ILLIXR_BUILD_SUFFIX} SHARED ${DEBUGVIEW_SOURCES})

if(BUILD_OPENCV)
    add_dependencies(plugin.debugview${ILLIXR_BUILD_SUFFIX} OpenCV_Viz)
    message("DEPENDS")
endif()

target_include_directories(plugin.debugview${ILLIXR_BUILD_SUFFIX} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS} ${glfw3_INCLUDE_DIRS} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR})
target_link_libraries(plugin.debugview${ILLIXR_BUILD_SUFFIX} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${OpenCV_LIBRARIES} ${glfw3_LIBRARIES} ${gl_LIBRARIES} dl pthread)
target_compile_features(plugin.debugview${ILLIXR_BUILD_SUFFIX} PRIVATE cxx_std_17)

install(TARGETS plugin.debugview${ILLIXR_BUILD_SUFFIX} DESTINATION lib)