# module to build and install the passthrough_integrator ILLIXR plugin
if(WIN32 OR MSVC)
    message(WARNING "passthrough_integrator is not yet available for windows")
    set(USE_ OFF)
else()

    set(PLUGIN_NAME plugin.passthrough_integrator${ILLIXR_BUILD_SUFFIX})

    # source files, listed individually so that any changes will trigger a rebuild
    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    )

    target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)
    target_link_libraries(${PLUGIN_NAME} Eigen3::Eigen)
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
