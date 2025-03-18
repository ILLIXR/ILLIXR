get_external_for_plugin(Monado_vk)
file(COPY_FILE ${CMAKE_SOURCE_DIR}/plugins/timewarp_vk/plugin.cpp ${CMAKE_BINARY_DIR}/plugins/timewarp_vk/plugin_m.cpp)
set(PLUGIN_NAME "plugin.timewarp_vk.monado${ILLIXR_BUILD_SUFFIX}")
set(TW_PLUGIN_DEFINITIONS "ENABLE_MONADO")
set(PLUGIN_SOURCE "${CMAKE_BINARY_DIR}/plugins/timewarp_vk/plugin_m.cpp")
set(TW_PREFIX "../")

include(../CMakeLists.txt)
