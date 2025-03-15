file(READ docs/modules.json MODULE_TEMP)
string(REPLACE "||" "\n" MODULE_PLUGIN_JSON "${MODULE_PLUGINS}")
string(REPLACE "@PROFILES@" "${MODULE_PLUGIN_JSON}" MODULE_JSON "${MODULE_TEMP}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/getting_started.md.in ${CMAKE_CURRENT_SOURCE_DIR}/docs/getting_started.md)

file(READ docs/index.md.in INDEX_TEMP)
string(REPLACE "https://illixr.github.io/ILLIXR/illixr_plugins/index.html" "illixr_plugins.md" INDEX_TEMP "${INDEX_TEMP}")
string(REPLACE "https://illixr.github.io/ILLIXR/illixr_services/index.html" "illixr_services.md" INDEX_TEMP "${INDEX_TEMP}")
string(REPLACE "(LICENSE)" "(LICENSE.md)" INDEX_TEMP "${INDEX_TEMP}")
string(REGEX REPLACE "https://illixr.github.io/ILLIXR/([a-zA-Z_/]*)/$" "\\1.md" INDEX_TEMP "${INDEX_TEMP}")
string(REPLACE "docs/docs/images/" "images/" INDEX_TEMP "${INDEX_TEMP}")

file(WRITE docs/index.md "${INDEX_TEMP}")

if(BUILD_DOCS)
    include(doxygen/CMakeLists.txt)
    if(BUILD_DOCS)
        include(mkdocs/CMakeLists.txt)
    endif()
    add_custom_target(docs
                      DEPENDS doc_doxygen doc_mkdocs
    )
endif()
