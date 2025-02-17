add_library(illixr_hmd OBJECT hmd.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/hmd.hpp
)

target_include_directories(illixr_hmd PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(illixr_serializable_data OBJECT serializable_data.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/serializable_data.hpp
)

target_include_directories(illixr_serializable_data PUBLIC ${CMAKE_SOURCE_DIR}/include)

if (ENABLE_VULKAN)
    add_library(illixr_vulkan_utils STATIC vulkan_utils.cpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_utils.hpp
    )

    target_include_directories(illixr_vulkan_utils PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(illixr_vulkan_utils PUBLIC spdlog::spdlog)
endif()
