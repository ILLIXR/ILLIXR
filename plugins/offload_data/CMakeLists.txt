# module to build and install the offload_data ILLIXR plugin

set(PLUGIN_NAME plugin.offload_data${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses/head_pose.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/global_module_defs.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)
target_link_libraries(${PLUGIN_NAME} PUBLIC
                      Boost::filesystem
                      Boost::serialization
                      Eigen3::Eigen
                      spdlog::spdlog
)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
