# hoisted from https://github.com/ILLIXR/okvis

cmake_minimum_required(VERSION 2.8)

# require OpenCV
find_package( OpenCV COMPONENTS core highgui imgproc features2d REQUIRED )
include_directories(BEFORE ${OpenCV_INCLUDE_DIRS}) 

# require okvis
find_package( okvis 1.1 REQUIRED)
include_directories(${OKVIS_INCLUDE_DIRS})

# require brisk
find_package( brisk 2 REQUIRED)
include_directories(${BRISK_INCLUDE_DIRS})

# require ceres
list(APPEND CMAKE_PREFIX_PATH ${OKVIS_INSTALLATION})
find_package( Ceres REQUIRED )
include_directories(${CERES_INCLUDE_DIRS}) 

# require OpenGV
find_package(opengv REQUIRED)

# VISensor, if available
list(APPEND CMAKE_MODULE_PATH ${OKVIS_INSTALLATION}/lib/CMake)
find_package(VISensor)
if(VISENSORDRIVER_FOUND)
  message(STATUS "Found libvisensor.")
else()
  message(STATUS "libvisensor not found")
endif()
