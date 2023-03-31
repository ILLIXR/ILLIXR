if(NOT ${Eigen3_FOUND})
    find_package(Eigen3 REQUIRED)
endif()

add_library(plugin.passthrough_integrator SHARED plugin.cpp)
target_include_directories(plugin.passthrough_integrator PRIVATE ${Eigen3_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR})
target_link_libraries(plugin.passthrough_integrator ${Eigen3_LIBRARIES})
target_compile_features(plugin.passthrough_integrator PRIVATE cxx_std_17)

install(TARGETS plugin.passthrough_integrator DESTINATION lib)
