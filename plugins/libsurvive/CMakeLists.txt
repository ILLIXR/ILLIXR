# To add compiler flags for debugging, uncomment the lines below.
# SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fsanitize=thread")
# SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")

CMAKE_MINIMUM_REQUIRED(VERSION 3.16)

set(PLUGIN_NAME plugin.libsurvive${ILLIXR_BUILD_SUFFIX})

SET(THREADS_PREFER_PTHREAD_FLAG TRUE)

find_package(PkgConfig)
pkg_check_modules(survive REQUIRED survive)

set(SRC_FILES plugin.cpp)

add_library(${PLUGIN_NAME} SHARED ${SRC_FILES})

target_include_directories(${PLUGIN_NAME} PUBLIC ${Eigen3_INCLUDE_DIRS} ${survive_INCLUDE_DIRS})

target_link_libraries(${PLUGIN_NAME} ${Eigen3_LIBRARIES} spdlog::spdlog Threads::Threads ${survive_LIBRARIES})
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)