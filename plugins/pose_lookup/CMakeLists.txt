add_library(plugin.pose_lookup SHARED plugin.cpp)
target_include_directories(plugin.pose_lookup PRIVATE ${ILLIXR_SOURCE_DIR})
target_compile_features(plugin.pose_lookup PRIVATE cxx_std_17)

install(TARGETS plugin.pose_lookup DESTINATION lib)
