set(PLUGIN_NAME plugin.frame_logger${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED
    plugin.cpp
    plugin.hpp
    ${CMAKE_SOURCE_DIR}/utils/vulkan_utils.cpp
    ${CMAKE_SOURCE_DIR}/include/illixr/gl_util/lib/stb_image.h
)

if(Vulkan_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
endif()

target_include_directories(${PLUGIN_NAME}
    PRIVATE
    ${ILLIXR_SOURCE_DIR}/include
    ${ILLIXR_SOURCE_DIR}/include/vk
    ${CMAKE_SOURCE_DIR}/utils
    third_party/
)

target_link_libraries(${PLUGIN_NAME} ${Vulkan_LIBRARIES} spdlog::spdlog)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
