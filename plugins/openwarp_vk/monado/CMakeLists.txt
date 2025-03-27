get_external_for_plugin(Monado_vk)
file(COPY_FILE ${CMAKE_SOURCE_DIR}/plugins/openwarp_vk/plugin.cpp ${CMAKE_BINARY_DIR}/plugins/openwarp_vk/plugin_m.cpp)
set(PLUGIN_NAME "plugin.openwarp_vk.monado${ILLIXR_BUILD_SUFFIX}")
set(OW_PLUGIN_DEFINITIONS MONADO_REQUIRED)
set(PLUGIN_SOURCE "${CMAKE_BINARY_DIR}/plugins/openwarp_vk/plugin_m.cpp")
set(OW_PREFIX "../")
set(OW_POSTFIX "/..")

include(../CMakeLists.txt)
