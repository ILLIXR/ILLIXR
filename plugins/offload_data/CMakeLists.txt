if(NOT ${Boost_FOUND})
    find_package(Boost REQUIRED)
endif()
add_library(plugin.offload_data SHARED plugin.cpp)
target_include_directories(plugin.offload_data PRIVATE ${Boost_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.offload_data boost_filesystem ${OpenCV_LIBRARIES})
target_compile_features(plugin.offload_data PRIVATE cxx_std_17)

install(TARGETS plugin.offload_data DESTINATION lib)
