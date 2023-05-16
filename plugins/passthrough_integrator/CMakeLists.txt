set(PLUGIN_NAME plugin.passthrough_integrator${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp)

target_include_directories(${PLUGIN_NAME} PRIVATE ${Eigen3_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR})
target_link_libraries(${PLUGIN_NAME} ${Eigen3_LIBRARIES})
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
