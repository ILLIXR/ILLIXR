file(READ docs/modules.json MODULE_TEMP)
string(REPLACE "||" "\n" MODULE_PLUGIN_JSON "${MODULE_PLUGINS}")
string(REPLACE "@PROFILES@" "${MODULE_PLUGIN_JSON}" MODULE_JSON "${MODULE_TEMP}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/getting_started.md.in ${CMAKE_CURRENT_SOURCE_DIR}/docs/getting_started.md)

if(BUILD_DOCS)
    include(doxygen/CMakeLists.txt)
    if(BUILD_DOCS)
        include(mkdocs/CMakeLists.txt)
    endif()
    add_custom_target(docs
            DEPENDS doc_doxygen doc_mkdocs
            )
endif()