# CMake file for building timewarp_gl plugin for ILLIXR
# Required packages:
#   pkg-config
#   X11 (development)
#   glew (development)
#   glu (development)
#   gl (development)
#   OpenCV including the viz module  (development)

if(NOT DEFINED BUILDING_ILLIXR)
    message(ERROR "The timewarp_gl plugin cannot be built outside of the main ILLIXR build")
endif()
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
file(GLOB_RECURSE TIMEWARP_SOURCES "./*.cpp")
add_library(plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX} SHARED ${TIMEWARP_SOURCES})

if(BUILD_OPENCV)
    add_dependencies(plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX} OPENCV_LIBRARY)
endif()

target_include_directories(plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES} ${OpenCV_LIBRARIES})
target_compile_features(plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX} PRIVATE cxx_std_17)

install(TARGETS plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
