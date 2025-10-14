# module to build and install the depthai ILLIXR plugin
if(WIN32 OR MSVC)
    message(WARNING "depthai is not yet available for windows")
    set(USE_ADA.DEPTHAI OFF)
else()

    get_external_for_plugin(DepthAI)

    set(PLUGIN_NAME plugin.depthai${ILLIXR_BUILD_SUFFIX})

    # source files, listed individually so that any changes will trigger a rebuild
    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
    )
    add_definitions(-Wno-format-extra-args)

    target_include_directories(${PLUGIN_NAME} PRIVATE ${DepthAI_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

    # if we are building DepthAI from source
    if(DepthAI_EXTERNAL)
        add_dependencies(${PLUGIN_NAME} DepthAI_ext cleanup_depthai_spdlog)
        target_link_libraries(${PLUGIN_NAME} PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE ${DepthAI_LIBRARIES} spdlog::spdlog)
    else()  # DepthAI already installed
        target_link_libraries(${PLUGIN_NAME} PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE depthai::core)
    endif()

    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
