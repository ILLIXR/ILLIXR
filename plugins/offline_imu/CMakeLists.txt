# module to build and install the offline_imu ILLIXR plugin
if(WIN32 OR MSVC)
    message(WARNING "offline_imu is not yet available for windows")
    set(USE_OFFLINE_IMU OFF)
else()

    set(PLUGIN_NAME plugin.offline_imu${ILLIXR_BUILD_SUFFIX})

    # source files, listed individually so that any changes will trigger a rebuild
    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_loading.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/managed_thread.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/iterators/csv_iterator.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/iterators/data_iterator.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
    )

    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
    target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
    target_link_libraries(${PLUGIN_NAME} ${Eigne3_LIBRARIES} spdlog::spdlog)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
