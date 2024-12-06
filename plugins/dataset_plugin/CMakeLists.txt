set(DATASET_PLUGIN_SOURCES
  src/config.cpp
  src/dataset_loader.cpp
  src/publisher_plugin.cpp
)

set(PLUGIN_NAME plugin.dataset-plugin${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED ${DATASET_PLUGIN_SOURCES})

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)