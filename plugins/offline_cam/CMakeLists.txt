# module to build and install the offline_cam ILLIXR plugin

set(PLUGIN_NAME plugin.offline_cam${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_loading.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/iterators/csv_iterator.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/iterators/data_iterator.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)
target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} Eigen3::Eigen spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
