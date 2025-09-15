# module to build and install the timewarp_gl ILLIXR plugin
if(NOT DEFINED BUILDING_ILLIXR)
    message(ERROR "The timewarp_gl plugin cannot be built outside of the main ILLIXR build")
endif()

if(NOT PLUGIN_NAME)
    set(PLUGIN_NAME "plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX}")
    set(PLUGIN_SOURCE "plugin.cpp")
    set(TW_PREFIX "")
endif()

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED
            ${PLUGIN_SOURCE}
            ${TW_PREFIX}plugin.hpp
            ${TW_PREFIX}shaders/timewarp_shader.hpp
            $<TARGET_OBJECTS:illixr_hmd_gl>
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/extended_window.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/global_module_defs.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/math_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/shader_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

if(TW_PLUGIN_DEFINITIONS)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE ${TW_PLUGIN_DEFINITIONS})
    target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include ${X11_INCLUDE_DIR})
target_link_libraries(${PLUGIN_NAME} spdlog::spdlog Eigen3::Eigen Boost::serialization)
    
if(WIN32 OR MSVC)
    target_link_libraries(${PLUGIN_NAME}
            GLEW::GLEW
            OpenGL::GL
            Vulkan::Vulkan
    )
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
else()
    target_include_directories(${PLUGIN_NAME} PRIVATE
            ${GLEW_INCLUDE_DIR}
            ${GLU_INCLUDE_DIR}
            ${gl_INCLUDE_DIRS}
            ${Vulkan_INCLUDE_DIRS}
    )
    target_link_libraries(${PLUGIN_NAME}
            ${X11_LIBRARIES}
            ${GLEW_LIBRARIES} 
            ${glu_LDFLAGS}
            ${gl_LIBRARIES}
            ${Vulkan_LIBRARY}
    )
    if(Vulkan_EXTERNAL)
        add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
    endif()
endif()
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
