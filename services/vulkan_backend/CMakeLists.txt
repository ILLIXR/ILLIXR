# module to build and install the Vulkan backend ILLIXR service

set(PLUGIN_NAME plugin.vulkan_backend${ILLIXR_BUILD_SUFFIX})

set(VULKAN_BACKEND_SOURCES 
    service.hpp    
    service.cpp
    vk_util.hpp
    vk_util.cpp
    ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/graphics_backend.hpp
)

add_library(${PLUGIN_NAME} SHARED ${VULKAN_BACKEND_SOURCES})

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)
target_include_directories(${PLUGIN_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PUBLIC spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
