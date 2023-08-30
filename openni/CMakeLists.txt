# OpenCV
find_package(OpenCV 3.4.6 EXACT)
include_directories(${OpenCV_INCLUDE_DIRS})

set(OPENNI_ROOT "/opt/ILLIXR/OpenNI" CACHE FILEPATH "Root directory of OpenNI2")

FIND_PATH(OPENNI_INCLUDE_DIR OpenNI.h HINTS "${OPENNI_ROOT}/Include")
FIND_LIBRARY(OPENNI_LIBRARY OpenNI2 HINTS "${OPENNI_ROOT}/Bin/x64-Release" "${OPENNI_ROOT}/Lib" "${OPENNI_ROOT}/Redist")

INCLUDE_DIRECTORIES(${OPENNI_INCLUDE_DIR})

add_definitions(-Wall -Wextra -Werror -Wno-format-extra-args -std=c++17 -g -O3)
add_library(plugin SHARED plugin.cpp)
target_link_libraries(plugin ${DEPENDENCIES} ${OpenCV_LIBRARIES} ${OPENNI_LIBRARY})