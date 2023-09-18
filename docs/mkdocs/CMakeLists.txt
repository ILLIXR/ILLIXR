find_package(PythonInterp)

find_program(MKDOCS_EXEC mkdocs
        HINTS "$ENV{HOME}/.local/bin")
include(GNUInstallDirs)

if(PythonInterp_FOUND AND MKDOCS_EXEC)
    set(MKDOCS_DOCS_DIR "${CURRENT_BUILD_DIR}/_docs/docs")
    set(MKDOCS_BUILD_DIR "${CURRENT_BUILD_DIR}/docs/docs" CACHE INTERNAL "")
    set(MKDOCS_INSTALL_DIR "${DOXYGEN_INSTALL_DIR}" CACHE INTERNAL "")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mkdocs/mkdocs.yaml.in ${CURRENT_BUILD_DIR}/_docs/mkdocs.yaml)
    #file(MAKE_DIRECTORY "${DOC_BUILD_DIR}")
    file(CREATE_LINK "${DOXYGEN_BUILD_DIR}" "${DOC_BUILD_DIR}/api" SYMBOLIC)
    add_custom_target(doc_mkdocs
            COMMAND ${MKDOCS_EXEC} build -c -f ${CURRENT_BUILD_DIR}/_docs/mkdocs.yaml
            DEPENDS doc_doxygen)
    install(DIRECTORY ${MKDOCS_BUILD_DIR}
            DESTINATION ${MKDOCS_INSTALL_DIR}
            OPTIONAL
            )
else()
    message(WARNING "mkdocs could not be found. ILLIXR documentation cannot be built.")
endif()