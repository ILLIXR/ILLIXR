include(GetGTest)

if(WIN32 OR MSVC)
    set(UNIT_TEST_RUNNER_NAME unit_test_runner${ILLIXR_BUILD_SUFFIX})
else()
    set(UNIT_TEST_RUNNER_NAME unit_test_runner${ILLIXR_BUILD_SUFFIX}.exe)
endif()

add_executable(${UNIT_TEST_RUNNER_NAME}
               unit_test_runner.cpp
               ${CMAKE_SOURCE_DIR}/include/illixr/dynamic_lib.hpp
)

target_include_directories(${UNIT_TEST_RUNNER_NAME} PRIVATE
                           ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(${UNIT_TEST_RUNNER_NAME} PRIVATE
                      gtest
                      spdlog::spdlog
                      Threads::Threads
)

if(NOT WIN32 AND NOT MSVC)
    target_link_libraries(${UNIT_TEST_RUNNER_NAME} PRIVATE
                          dl
                          stdc++fs
    )
endif()

target_compile_features(${UNIT_TEST_RUNNER_NAME} PRIVATE cxx_std_17)

set_target_properties(${UNIT_TEST_RUNNER_NAME} PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/unit_tests
)

add_dependencies(unit_tests ${UNIT_TEST_RUNNER_NAME})
