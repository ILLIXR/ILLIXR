# module to build and install the pose_lookup ILLIXR plugin

set(PLUGIN_NAME plugin.pose_lookup${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            ../../include/illixr/data_loading.hpp
            utils.hpp
            ../../include/illixr/error_util.hpp
            ../../include/illixr/csv_iterator.hpp
            ../../include/illixr/data_format.hpp
            ../../include/illixr/plugin.hpp
            ../../include/illixr/global_module_defs.hpp
            ../../include/illixr/phonebook.hpp
            ../../include/illixr/pose_prediction.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${Eigen3_LIBRARIES} spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
