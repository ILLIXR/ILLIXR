# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
#
# SystemTypes.cmake
#
# Scans include/illixr/data_format/*.hpp via generate_system_types.py and
# writes one YAML per header into interfaces/system/illixr/data_format/.
#
# These YAML files are committed to git and describe ILLIXR system struct fields
# in the same schema as bridge type YAMLs.  They allow the bridge generator to
# produce correct dict conversion code for system types without requiring
# libclang at CMake configure time on every developer's machine.
#
# This module only re-runs the scan when data_format/ headers have changed
# (tracked via a JSON state file in the system/ output directory).
#
# Usage (include before PythonBridge.cmake):
#   set(ILLIXR_SOURCE_DIR ${CMAKE_SOURCE_DIR})
#   set(SYSTEM_TYPES_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/interfaces/system)
#   include(interfaces/SystemTypes.cmake)
#
# Variables read:
#   ILLIXR_SOURCE_DIR          Root of the ILLIXR repository
#   SYSTEM_TYPES_OUTPUT_DIR    Where to write system/ YAML files
#                              (default: ${CMAKE_SOURCE_DIR}/interfaces/system)
#
# Variables set after inclusion:
#   SYSTEM_TYPES_YAML_DIR      Path to the written illixr/data_format/ YAML dir

cmake_minimum_required(VERSION 3.16)

if(DEFINED _ILLIXR_SYSTEM_BRIDGE_INCLUDED)
    return()
endif()
set(_ILLIXR_SYSTEM_BRIDGE_INCLUDED TRUE)

# ---- Locate the generator script ----
get_filename_component(_ST_SCRIPT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(_ST_SCRIPT "${_ST_SCRIPT_DIR}/generate_illixr_types.py")

if(NOT EXISTS "${_ST_SCRIPT}")
    message(FATAL_ERROR "SystemTypes.cmake: generator script not found at ${_ST_SCRIPT}")
endif()

# ---- Defaults ----
if(NOT DEFINED ILLIXR_SOURCE_DIR)
    set(ILLIXR_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
endif()

if(NOT DEFINED SYSTEM_TYPES_OUTPUT_DIR)
    set(SYSTEM_TYPES_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/interfaces/system")
endif()

set(SYSTEM_TYPES_YAML_DIR "${SYSTEM_TYPES_OUTPUT_DIR}/illixr/data_format")

# ---- Find Python ----
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_FOUND)
    message(WARNING "SystemTypes.cmake: Python3 not found — system type YAMLs will not be regenerated")
    return()
endif()

# ---- Staleness check ----
# The generate_system_types.py script manages its own JSON state file
# (.system_types_state.json) with MD5 hashes of all data_format headers.
# We call it unconditionally; it exits quickly if nothing has changed.
# For a tighter CMake integration, we also glob the headers here so that
# CMake itself re-runs configure when a header is added or removed.

file(GLOB _DATA_FORMAT_HEADERS
    "${ILLIXR_SOURCE_DIR}/include/illixr/data_format/*.hpp")

if(NOT _DATA_FORMAT_HEADERS)
    message(STATUS "SystemTypes.cmake: no data_format headers found — skipping")
    return()
endif()

message(STATUS "SystemTypes: checking data_format headers for changes...")

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${_ST_SCRIPT}"
        --source-dir "${ILLIXR_SOURCE_DIR}"
        --output-dir "${SYSTEM_TYPES_OUTPUT_DIR}"
    RESULT_VARIABLE _ST_RESULT
    OUTPUT_VARIABLE _ST_OUTPUT   # script writes cmake output to stdout
    ERROR_VARIABLE  _ST_STDERR   # progress messages go to stderr
)

# Echo progress messages from the script
if(_ST_STDERR)
    message(STATUS "${_ST_STDERR}")
endif()

if(NOT _ST_RESULT EQUAL 0)
    message(FATAL_ERROR
        "SystemTypes.cmake: generate_system_types.py failed (exit ${_ST_RESULT}).\n"
        "Make sure libclang and the clang Python package are installed:\n"
        "  pip install clang\n"
        "Or commit the pre-generated system/ YAML files and skip regeneration\n"
        "by setting SYSTEM_TYPES_SKIP_REGEN=ON.")
endif()

# Re-run CMake configure when any data_format header changes
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${_DATA_FORMAT_HEADERS})

message(STATUS "SystemTypes: YAML files are up-to-date at ${SYSTEM_TYPES_YAML_DIR}")
