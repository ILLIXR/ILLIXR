# To add compiler flags for debugging, uncomment the lines below.
# SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fsanitize=thread")
# SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")

set(PLUGIN_NAME plugin.lighthouse${ILLIXR_BUILD_SUFFIX})

SET(THREADS_PREFER_PTHREAD_FLAG TRUE)

get_external_for_plugin(LibSurvive)

set(SRC_FILES plugin.cpp
    plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses/head_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

add_library(${PLUGIN_NAME} SHARED ${SRC_FILES})

target_include_directories(${PLUGIN_NAME} PUBLIC
                           ${CMAKE_INSTALL_PREFIX}/include
                           ${survive_INCLUDE_DIRS}
                           ${survive_INCLUDE_DIRS}/redist
                           ${libsurvive_SOURCE_DIR}/include/libsurvive
                           ${libsurvive_SOURCE_DIR}/redist
)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      Eigen3::Eigen
                      spdlog::spdlog
                      Threads::Threads
                      ${survive_LIBRARIES}
                      Boost::serialization
)

if (WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY BUILDING_SURVIVE_PLUGIN)
endif ()
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
