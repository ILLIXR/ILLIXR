if(NOT ${OpenCV_FOUND})
    find_package(OpenCV REQUIRED)
endif()

add_library(plugin.offline_cam SHARED plugin.cpp)
target_include_directories(plugin.offline_cam PRIVATE ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR})
target_link_libraries(plugin.offline_cam ${OpenCV_LIBRARIES})
target_compile_features(plugin.offline_cam PRIVATE cxx_std_17)

install(TARGETS plugin.offline_cam DESTINATION lib)
