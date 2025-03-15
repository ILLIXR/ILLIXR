find_package(Python3)

find_program(MKDOCS_EXEC mkdocs
             HINTS "$ENV{HOME}/.local/bin" "/usr/bin/mkdocs" "/usr/local/bin/mkdocs")
include(GNUInstallDirs)

if(Python3_FOUND AND MKDOCS_EXEC)
    set(MKDOCS_DOCS_DIR "${CURRENT_BUILD_DIR}/_docs/docs")
    set(MKDOCS_BUILD_DIR "${CURRENT_BUILD_DIR}/docs/docs" CACHE INTERNAL "")
    set(MKDOCS_INSTALL_DIR "${DOXYGEN_INSTALL_DIR}" CACHE INTERNAL "")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mkdocs/mkdocs.yaml.in ${CURRENT_BUILD_DIR}/_docs/mkdocs.yaml)
    add_custom_target(doc_mkdocs
                      COMMAND ${MKDOCS_EXEC} build -c -f ${CURRENT_BUILD_DIR}/_docs/mkdocs.yaml
                      DEPENDS doc_doxygen)

    # set up redirects for moved pages
    file(MAKE_DIRECTORY ${MKDOCS_DOCS_DIR}/ILLIXR)
    file(GLOB_RECURSE MD_FILES RELATIVE ${CMAKE_SOURCE_DIR}/docs/docs "*.md")
    foreach(MD_FILE IN LISTS MD_FILES)
        set(CURRENT_MD_FILE "${MKDOCS_DOCS_DIR}/ILLIXR/${MD_FILE}")
        string(REGEX MATCHALL "/" MATCHES "${MD_FILE}")
        list(LENGTH MATCHES COUNT)
        set(PREPEND "")
        foreach(VAR RANGE ${COUNT})
            set(PREPEND "${PREPEND}../")
        endforeach()
        string(REPLACE ".md" "" STRIPPED_FILE "${MD_FILE}")
        file(WRITE ${CURRENT_MD_FILE} "This file has moved to [https://illixr.github.io/${STRIPPED_FILE}][RD10]\n\n")
        file(APPEND ${CURRENT_MD_FILE} "You should be automatically redirected in 5 seconds.\n")
        file(APPEND ${CURRENT_MD_FILE} "<script>\n    let timer = setTimeout(function() {\n        window.location=\"https://illixr.github.io/${STRIPPED_FILE}\"},\n        5000);\n</script>\n\n")
        file(APPEND ${CURRENT_MD_FILE} "[RD10]:  https://illixr.github.io/${STRIPPED_FILE}/")
    endforeach()
else()
    message(WARNING "mkdocs could not be found. ILLIXR documentation cannot be built.")
endif()
