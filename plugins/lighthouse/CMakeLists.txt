# To add compiler flags for debugging, uncomment the lines below.
# SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fsanitize=thread")
# SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
if(WIN32 OR MSVC OR ANDROID)
    message(WARNING "lighthouse is not yet available for Windows or Android")
    set(USE_LIGHTHOUSE OFF)
    return()
endif()

set(PLUGIN_NAME plugin.lighthouse${ILLIXR_BUILD_SUFFIX})

set(THREADS_PREFER_PTHREAD_FLAG TRUE)

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

if(LibSurvive_EXTERNAL)
    message("SURVIVE   ${survive_INCLUDE_DIRS} ${survive_INCLUDE_DIRS}/redist ${survive_LIBRARIES}")
    add_dependencies(${PLUGIN_NAME} ${LibSurvive_DEP_STR})
endif()

target_link_libraries(${PLUGIN_NAME} Eigen3::Eigen spdlog::spdlog Threads::Threads ${survive_LIBRARIES})
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
