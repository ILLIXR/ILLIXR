# OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

set(OPEN_NI_ROOT "/usr/local" CACHE FILEPATH "Root directory of OpenNI2")

find_library(OpenNI_LIBRARY
NAMES OpenNI
PATHS "${OPEN_NI_ROOT}/Lib" "C:/Program Files (x86)/OpenNI/Lib" "C:/Program Files/OpenNI/Lib" ${CMAKE_LIB_PATH}
)
FIND_PATH(OPENNI_ROOT LICENSE HINTS /usr/local)

FIND_PATH(OPENNI_INCLUDE_DIR OpenNI.h HINTS "${OPENNI_ROOT}/Include")
FIND_LIBRARY(OPENNI_LIBRARY OpenNI2 HINTS "${OPENNI_ROOT}/Bin/x64-Release" "${OPENNI_ROOT}/Lib" "${OPENNI_ROOT}/Redist")

INCLUDE_DIRECTORIES(${OPENNI_INCLUDE_DIR})

add_definitions(-Wall -Wextra -Werror -Wno-format-extra-args -std=c++17 -g -O3)
add_library(plugin SHARED plugin.cpp)
target_link_libraries(plugin ${DEPENDENCIES} ${OpenCV_LIBRARIES} ${OPENNI_LIBRARY})