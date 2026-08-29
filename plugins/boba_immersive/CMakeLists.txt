if(WIN32 OR MSVC)
    message(WARNING "boba_immersive is currently available only on Linux")
    set(USE_BOBA_IMMERSIVE OFF)
else()
    set(PLUGIN_NAME plugin.boba_immersive${ILLIXR_BUILD_SUFFIX})

    add_library(${PLUGIN_NAME} SHARED
                plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/openxr_view_frame.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/quest_controller.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/stereo_frame.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/stoplight.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    )

    target_link_libraries(${PLUGIN_NAME} PRIVATE
                          Eigen3::Eigen
                          Threads::Threads
                          spdlog::spdlog
    )
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/scripts/setup_boba_immersive.sh DESTINATION bin)
endif()
