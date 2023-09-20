add_executable(main${ILLIXR_BUILD_SUFFIX}.exe
        src/main.cpp
        src/illixr.hpp
        src/cxxopts.hpp
        ../include/illixr/runtime.hpp
        ../include/illixr/phonebook.hpp
        )
add_library(plugin.main${ILLIXR_BUILD_SUFFIX} SHARED
        src/plugin.cpp
        src/runtime_impl.cpp
        src/illixr.hpp
        src/cxxopts.hpp
        ../include/illixr/runtime.hpp
        ../include/illixr/phonebook.hpp
        )

##############
# yaml parser
##############

get_external(Yamlcpp)

target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PRIVATE "ILLIXR_VISUALIZERS='${VIS_NAMES}'")

target_include_directories(main${ILLIXR_BUILD_SUFFIX}.exe PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(main${ILLIXR_BUILD_SUFFIX}.exe dl stdc++fs plugin.main${ILLIXR_BUILD_SUFFIX})

target_include_directories(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${SQLite3_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${yaml-cpp_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(plugin.main${ILLIXR_BUILD_SUFFIX} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${SQLite3_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES} dl Threads::Threads stdc++fs ${yaml-cpp_LIBRARIES} spdlog::spdlog)

install(TARGETS main${ILLIXR_BUILD_SUFFIX}.exe DESTINATION bin)
install(TARGETS plugin.main${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
