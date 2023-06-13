set(PLUGIN_NAME plugin.pose_lookup${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        data_loading.hpp
        utils.hpp
        ../../include/illixr/error_util.hpp
        ../../include/illixr/csv_iterator.hpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/plugin.hpp
        ../../include/illixr/global_module_defs.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/pose_prediction.hpp
        )

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${Eigen3_LIBRARIES} ${OpenCV_LIBRARIES})
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
