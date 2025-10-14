# module to build and install the ground_truth_slam ILLIXR plugin
if(WIN32 OR MSVC)
    message(WARNING "ground_truth_slam is not yet available for windows")
    set(USE_GROUND_TRUTH_SLAM OFF)
else()

    set(PLUGIN_NAME plugin.ground_truth_slam${ILLIXR_BUILD_SUFFIX})

    # source files, listed individually so that any changes will trigger a rebuild
    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_loading.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/iterators/csv_iterator.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/iterators/data_iterator.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
    )

    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
    target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)
    target_link_libraries(${PLUGIN_NAME} Eigen3::Eigen spdlog::spdlog)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
