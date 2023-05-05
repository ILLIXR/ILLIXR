add_library(plugin.pose_lookup${ILLIXR_BUILD_SUFFIX} SHARED plugin.cpp)
target_include_directories(plugin.pose_lookup${ILLIXR_BUILD_SUFFIX} PRIVATE ${ILLIXR_SOURCE_DIR})
target_compile_features(plugin.pose_lookup${ILLIXR_BUILD_SUFFIX} PRIVATE cxx_std_17)

install(TARGETS plugin.pose_lookup${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
