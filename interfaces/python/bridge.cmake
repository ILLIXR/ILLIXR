# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
#
# bridge.cmake
#
# Provides generate_python_bridge_plugins() which integrates with the
# existing ILLIXR cmake infrastructure.
#
# Behaviour
# ---------
#   If -DPYTHON_BRIDGE_PROFILE is not set: returns immediately, nothing done.
#
#   If -DPYTHON_BRIDGE_PROFILE=<name>.yaml is set:
#
#     Tier 1 — Profile yaml generation (timestamp-gated)
#       Mirrors the generate_yaml() pattern in HelperFunctions.cmake.
#       Only runs when python_bridges.yaml has changed since last configure
#       (tracked via LAST_PY_PROFILES_YAML_BUILD cache variable).
#
#     Tier 2 — Struct headers + plugin sources (always runs)
#       Generates for every bridge listed in the selected profile:
#         ${CMAKE_BINARY_DIR}/include/illixr/bridge/<type>.hpp
#         ${CMAKE_BINARY_DIR}/generated/plugins/<bridge>/plugin.hpp
#         ${CMAKE_BINARY_DIR}/generated/plugins/<bridge>/plugin.cpp
#         ${CMAKE_BINARY_DIR}/generated/plugins/<bridge>/bindings_<type>.cpp
#
#     System bindings (included automatically if present)
#       interfaces/python/system_bindings/bindings_*.cpp are compiled into
#       every bridge plugin target alongside the bridge-specific sources.
#       utils/serialization/*.cpp files listed in SystemBindings.cmake are
#       also linked in.
#
# -DPYTHON_BRIDGE_PROFILE interpretation
#   Filename only, .yaml extension required.
#   Resolved as: ${CMAKE_SOURCE_DIR}/interfaces/python/profiles/<value>
#   Bridge descriptor yamls remain in interfaces/python/bridges/.
#   Generated per-profile yamls live in interfaces/python/profiles/.
#
# Placement in CMakeLists.txt
#   Call generate_python_bridge_plugins() AFTER generate_yaml(),
#   check_plugins(), and read_yaml() but BEFORE the plugin
#   add_subdirectory loop.

cmake_minimum_required(VERSION 3.22)
find_package(Python COMPONENTS Interpreter Development REQUIRED)
set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)

if(DEFINED _ILLIXR_PYTHON_BRIDGE_INCLUDED)
    return()
endif()
set(_ILLIXR_PYTHON_BRIDGE_INCLUDED TRUE)

set(_PY_BRIDGE_GENERATOR
    "${CMAKE_SOURCE_DIR}/interfaces/python/generate_python_bridges.py"
    CACHE INTERNAL "Path to generate_python_bridges.py")

set(_PY_BRIDGE_MASTER_PROFILE
    "${CMAKE_SOURCE_DIR}/interfaces/python/python_profiles.yaml"
    CACHE INTERNAL "Path to master python bridge profile")

set(_PY_BRIDGE_BRIDGES_DIR
    "${CMAKE_SOURCE_DIR}/interfaces/python/bridges"
    CACHE INTERNAL "Directory containing bridge descriptor yamls")

set(_PY_BRIDGE_PROFILES_DIR
    "${CMAKE_SOURCE_DIR}/interfaces/python/profiles"
    CACHE INTERNAL "Directory containing generated per-profile yamls")

set(_PY_SYSTEM_BINDINGS_DIR
    "${CMAKE_SOURCE_DIR}/interfaces/python/system_bindings"
    CACHE INTERNAL "Directory containing generated system binding sources")

set(_PY_SYSTEM_BINDINGS_CMAKE
    "${CMAKE_SOURCE_DIR}/interfaces/python/system_bindings/SystemBindings.cmake"
    CACHE INTERNAL "Auto-generated cmake fragment for system bindings")

# ---------------------------------------------------------------------------
# _py_bridge_collect_system_sources()
#
# Internal helper.  Populates _PY_SYSTEM_SOURCES in the calling scope with
# all system binding .cpp files and serialization .cpp files declared in
# SystemBindings.cmake.  Safe to call even when system bindings have not
# been generated (returns empty list).
# ---------------------------------------------------------------------------
macro(_py_bridge_collect_system_sources)
    set(_PY_SYSTEM_SOURCES "")

    if(EXISTS "${_PY_SYSTEM_BINDINGS_CMAKE}")
        # SystemBindings.cmake sets _ILLIXR_SYSTEM_BINDING_SOURCES and
        # _ILLIXR_SERIALIZATION_SOURCES
        include("${_PY_SYSTEM_BINDINGS_CMAKE}")
        if(_ILLIXR_SYSTEM_BINDING_SOURCES)
            list(APPEND _PY_SYSTEM_SOURCES ${_ILLIXR_SYSTEM_BINDING_SOURCES})
        endif()
        if(_ILLIXR_SERIALIZATION_SOURCES)
            list(APPEND _PY_SYSTEM_SOURCES ${_ILLIXR_SERIALIZATION_SOURCES})
        endif()
    else()
        message(STATUS
                "python/bridge: No system bindings found at "
                "${_PY_SYSTEM_BINDINGS_DIR}. "
                "Run generate_system_bindings.py to generate them.")
    endif()
endmacro()

# ---------------------------------------------------------------------------
# generate_python_bridge_plugins()
# ---------------------------------------------------------------------------
function(generate_python_bridge_plugins)

    # -----------------------------------------------------------------------
    # 0. Nothing to do if no profile selected
    # -----------------------------------------------------------------------
    if(NOT PYTHON_BRIDGE_PROFILE)
        return()
    endif()

    # -----------------------------------------------------------------------
    # 1. Resolve the bridge profile path
    # -----------------------------------------------------------------------
    set(_bridge_profile
        "${_PY_BRIDGE_PROFILES_DIR}/${PYTHON_BRIDGE_PROFILE}")

    # -----------------------------------------------------------------------
    # 2. Prerequisite checks
    # -----------------------------------------------------------------------
    if(NOT Python3_EXECUTABLE)
        message(FATAL_ERROR
                "generate_python_bridge_plugins: Python3 interpreter not found. "
                "Call find_package(Python3 REQUIRED COMPONENTS Interpreter) "
                "before including PythonBridge.cmake.")
    endif()

    if(NOT EXISTS "${_PY_BRIDGE_GENERATOR}")
        message(FATAL_ERROR
                "generate_python_bridge_plugins: generator script not found: "
                "${_PY_BRIDGE_GENERATOR}")
    endif()

    if(NOT EXISTS "${_PY_BRIDGE_MASTER_PROFILE}")
        message(FATAL_ERROR
                "generate_python_bridge_plugins: master profile not found: "
                "${_PY_BRIDGE_MASTER_PROFILE}")
    endif()

    # -----------------------------------------------------------------------
    # 3. Tier 1 — Profile yaml generation (timestamp-gated on master profile)
    # -----------------------------------------------------------------------
    file(MD5 "${_PY_BRIDGE_MASTER_PROFILE}" _py_bridge_master_hash)

    if(NOT DEFINED CACHE{LAST_PY_PROFILES_YAML_BUILD})
        set(LAST_PY_PROFILES_YAML_BUILD "" CACHE INTERNAL "")
    endif()

    if(NOT "${LAST_PY_PROFILES_YAML_BUILD}" STREQUAL "${_py_bridge_master_hash}")
        message(STATUS "Rebuilding python bridge profile yaml files from python_profiles.yaml")
        file(MAKE_DIRECTORY "${_PY_BRIDGE_PROFILES_DIR}")

        execute_process(
                COMMAND "${Python3_EXECUTABLE}"
                "${_PY_BRIDGE_GENERATOR}"
                --write-profiles
                "${_PY_BRIDGE_MASTER_PROFILE}"
                "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE _tier1_out
                ERROR_VARIABLE _tier1_err
                RESULT_VARIABLE _tier1_result
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(NOT _tier1_result EQUAL 0)
            message(FATAL_ERROR
                    "generate_python_bridge_plugins: profile generation failed:\n"
                    "${_tier1_err}\n${_tier1_out}")
        endif()
        if(_tier1_err)
            message(WARNING "${_tier1_err}")
        endif()

        file(MD5 "${_PY_BRIDGE_MASTER_PROFILE}" _master_md5)
        set(LAST_PY_PROFILES_YAML_BUILD "${_master_md5}"
            CACHE INTERNAL "MD5 of python_profiles.yaml at last processing")
    endif()

    if(NOT EXISTS "${_bridge_profile}")
        message(FATAL_ERROR
                "PYTHON_BRIDGE_PROFILE '${PYTHON_BRIDGE_PROFILE}' was not found "
                "at ${_bridge_profile}.\n"
                "Generated profile yamls live in interfaces/python/profiles/.\n"
                "Available profiles are defined in "
                "${_PY_BRIDGE_MASTER_PROFILE}.")
    endif()

    # -----------------------------------------------------------------------
    # 4. Tier 2 — Struct headers + plugin sources
    #
    # Staleness detection is delegated entirely to the Python generator via
    # a JSON state file in the build directory.  This avoids all cmake cache
    # scoping, escaping, and indirect-expansion pitfalls.
    #
    # The generator writes ${CMAKE_BINARY_DIR}/.py_bridge_state.json after
    # each successful run.  On the next invocation it reads that file,
    # computes MD5 hashes of all bridge and type yaml files, and compares
    # them to determine which bridges are stale.  It then regenerates only
    # those bridges and emits cmake set() variables for ALL bridges.
    # -----------------------------------------------------------------------
    message(STATUS "Running Python bridge generator...")
    execute_process(
            COMMAND "${Python3_EXECUTABLE}"
            "${_PY_BRIDGE_GENERATOR}"
            --generate
            "${_bridge_profile}"
            "${CMAKE_BINARY_DIR}"
            "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _tier2_out
            ERROR_VARIABLE _tier2_err
            RESULT_VARIABLE _tier2_result
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(_tier2_err)
        # Generator prints per-bridge progress to stderr — show as STATUS
        string(REPLACE "
        " ";" _tier2_err_lines "${_tier2_err}")
        foreach(_err_line ${_tier2_err_lines})
            if(NOT "${_err_line}" STREQUAL "")
                message(STATUS "${_err_line}")
            endif()
        endforeach()
    endif()

    if(NOT _tier2_result EQUAL 0)
        message(FATAL_ERROR
                "generate_python_bridge_plugins: code generation failed:\n"
                "${_tier2_err}\n${_tier2_out}")
    endif()
    # (stderr already printed as STATUS messages above)

    cmake_language(EVAL CODE "${_tier2_out}")

    # -----------------------------------------------------------------------
    # 5. Validate that all required variables are now set
    #    (either from the generator or restored from cache)
    # -----------------------------------------------------------------------
    foreach(_required_var
            PY_BRIDGE_NAMES PY_BRIDGE_CMAKE_DIRS PY_BRIDGE_COUNT)
        if(NOT DEFINED ${_required_var})
            message(FATAL_ERROR
                    "generate_python_bridge_plugins: '${_required_var}' is not set "
                    "and no cached value exists. Delete CMakeCache.txt and re-run cmake.")
        endif()
    endforeach()

    # -----------------------------------------------------------------------
    # 6. Register each bridge plugin via add_subdirectory
    #    Each bridge's generated CMakeLists.txt defines its own target,
    #    links, includes, and compile definitions — matching the ILLIXR
    #    plugin pattern exactly (SHARED library, PLUGIN_NAME definition,
    #    install rule).
    # -----------------------------------------------------------------------
    list(LENGTH PY_BRIDGE_NAMES _n_bridges)
    if(_n_bridges EQUAL 0)
        return()
    endif()
    math(EXPR _last_idx "${_n_bridges} - 1")

    foreach(_idx RANGE ${_last_idx})
        list(GET PY_BRIDGE_NAMES ${_idx} _bname)
        list(GET PY_BRIDGE_CMAKE_DIRS ${_idx} _cmake_dir)

        # The generated CMakeLists.txt lives in _cmake_dir alongside
        # plugin.cpp, plugin.hpp, and bindings_*.cpp.
        add_subdirectory("${_cmake_dir}" "${CMAKE_BINARY_DIR}/bridge_build/${_bname}")

        # -----------------------------------------------------------------------
        # 7. Append to PLUGIN_UNORDERED -> included in illixr.yaml
        # -----------------------------------------------------------------------
        if(PLUGIN_UNORDERED)
            set(PLUGIN_UNORDERED "${PLUGIN_UNORDERED},${_bname}"
                PARENT_SCOPE)
        else()
            set(PLUGIN_UNORDERED "${_bname}" PARENT_SCOPE)
        endif()

        message(STATUS "Python bridge plugin '${_bname}' configured")
    endforeach()

    # -----------------------------------------------------------------------
    # 9. Re-configure triggers
    # -----------------------------------------------------------------------
    set_property(DIRECTORY APPEND PROPERTY
                 CMAKE_CONFIGURE_DEPENDS "${_PY_BRIDGE_MASTER_PROFILE}")
    set_property(DIRECTORY APPEND PROPERTY
                 CMAKE_CONFIGURE_DEPENDS "${_bridge_profile}")

    if(EXISTS "${_PY_SYSTEM_BINDINGS_CMAKE}")
        set_property(DIRECTORY APPEND PROPERTY
                     CMAKE_CONFIGURE_DEPENDS "${_PY_SYSTEM_BINDINGS_CMAKE}")
    endif()

    # Track bridge descriptor yamls and their type yamls
    file(STRINGS "${_bridge_profile}" _bp_content)
    foreach(_line ${_bp_content})
        string(REGEX MATCH "bridges:[ \t]*(.*)" _m "${_line}")
        if(CMAKE_MATCH_1)
            string(REPLACE "," ";" _bnames "${CMAKE_MATCH_1}")
            foreach(_bn ${_bnames})
                string(STRIP "${_bn}" _bn)
                set(_bdesc "${_PY_BRIDGE_BRIDGES_DIR}/${_bn}.yaml")
                if(EXISTS "${_bdesc}")
                    set_property(DIRECTORY APPEND PROPERTY
                                 CMAKE_CONFIGURE_DEPENDS "${_bdesc}")
                    file(STRINGS "${_bdesc}" _bd_lines)
                    foreach(_bd_line ${_bd_lines})
                        string(STRIP "${_bd_line}" _bd_line)
                        string(REGEX MATCH
                               "^-[ \t]+([a-z][a-z0-9_]*)$"
                               _tm "${_bd_line}")
                        if(CMAKE_MATCH_1)
                            set(_tpath
                                "${CMAKE_SOURCE_DIR}/interfaces/data/"
                                "${CMAKE_MATCH_1}.yaml")
                            if(EXISTS "${_tpath}")
                                set_property(DIRECTORY APPEND PROPERTY
                                             CMAKE_CONFIGURE_DEPENDS "${_tpath}")
                            endif()
                        endif()
                    endforeach()
                endif()
            endforeach()
        endif()
    endforeach()

endfunction()
