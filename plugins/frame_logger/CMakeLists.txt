add_library(frame_logger SHARED
    plugin.cpp
)

target_link_libraries(frame_logger
    PRIVATE
    illixr_common
    stb_image_write
)

target_include_directories(frame_logger
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

install(TARGETS frame_logger
    LIBRARY DESTINATION lib/illixr/plugins
) 