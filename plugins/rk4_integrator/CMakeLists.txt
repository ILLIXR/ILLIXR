target_include_directories(plugin.rk4_integrator${ILLIXR_BUILD_SUFFIX} PRIVATE ${ILLIXR_SOURCE_DIR})
target_compile_features(plugin.rk4_integrator${ILLIXR_BUILD_SUFFIX} PRIVATE cxx_std_17)

install(TARGETS plugin.rk4_integrator${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
