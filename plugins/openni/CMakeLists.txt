pkg_check_modules(libopenni2 REQUIRED libopenni2)

set(PLUGIN_NAME plugin.openni${ILLIXR_BUILD_SUFFIX})

set(OPENNI_SOURCES plugin.cpp
    plugin.hpp
    ../../include/illixr/data_format.hpp
    ../../include/illixr/phonebook.hpp
    ../../include/illixr/relative_clock.hpp
    ../../include/illixr/switchboard.hpp
    ../../include/illixr/threadloop.hpp
)

add_library(${PLUGIN_NAME} SHARED ${OPENNI_SOURCES})

target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS} ${libopenni2_INCLUDE_DIR})
target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} ${libopenni2_LIBRARY} spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
