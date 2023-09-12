include(GNUInstallDirs)

find_package(Doxygen QUIET)
if(Doxygen_FOUND)
    message("${CMAKE_SOURCE_DIR} _____")
    message("${CMAKE_CURRENT_BINARY_DIR} _____")
    #message("${CMAKE_CURR}")
    file(MAKE_DIRECTORY "${CURRENT_BUILD_DIR}/docs/api")
    set(DOC_BUILD_DIR "${CURRENT_BUILD_DIR}/_docs/docs" CACHE INTERNAL "")
    #file(GLOB DOC_FILES "${CMAKE_SOURCE_DIR}/docs/docs/*")
    #file(COPY ${DOC_FILES} DESTINATION ${DOC_BUILD_DIR} FOLLOW_SYMLINK_CHAIN)
    set(DOXYGEN_BUILD_DIR "${CURRENT_BUILD_DIR}/docs/api" CACHE INTERNAL "")
    set(DOXYGEN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/doc/ILLIXR/api" CACHE INTERNAL "")
    message("${DOXYGEN_INSTALL_DIR}  __${CMAKE_INSTALL_DATAROOTDIR}__")
    set(DOXY_DOC_DIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/ILLIXR" CACHE INTERNAL "")
    set(DOXYGEN_SOURCE_DIR "${CMAKE_SOURCE_DIR}/include/illixr")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doxygen/Doxyfile.in ${CURRENT_BUILD_DIR}/_docs/Doxyfile)
    execute_process(COMMAND cp -rL ${CMAKE_SOURCE_DIR}/docs/docs/ ${DOC_BUILD_DIR}/)
    add_custom_target(doc_doxygen
            COMMAND ${DOXYGEN_EXECUTABLE} ${CURRENT_BUILD_DIR}/_docs/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            VERBATIM)
    install(DIRECTORY ${DOXYGEN_BUILD_DIR}/html
            DESTINATION ${DOXYGEN_INSTALL_DIR}
            OPTIONAL
            )
else()
    message(WARNING "Cannot find doxygen, documentation cannot be built")
    set(BUILD_DOCS NO CACHE BOOL "")
endif()