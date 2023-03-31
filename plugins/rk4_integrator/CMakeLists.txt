add_library(plugin.rk4_integrator SHARED plugin.cpp)
target_include_directories(plugin.rk4_integrator PRIVATE ${ILLIXR_SOURCE_DIR})
target_compile_features(plugin.rk4_integrator PRIVATE cxx_std_17)

install(TARGETS plugin.rk4_integrator DESTINATION lib)
