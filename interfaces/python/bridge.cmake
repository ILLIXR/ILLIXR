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
#       (tracked via LAST_PY_BRIDGE_YAML_BUILD cache variable).
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

if(DEFINED _ILLIXR_PYTHON_BRIDGE_INCLUDED)
    return()
endif()
set(_ILLIXR_PYTHON_BRIDGE_INCLUDED TRUE)

set(_PY_BRIDGE_GENERATOR
    "${CMAKE_SOURCE_DIR}/interfaces/python/generate_python_bridges.py"
    CACHE INTERNAL "Path to generate_python_bridges.py")

set(_PY_BRIDGE_MASTER_PROFILE
    "${CMAKE_SOURCE_DIR}/interfaces/python/python_bridges.yaml"
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
                "before including python/bridge.cmake.")
    endif()

    if(NOT TARGET pybind11::embed)
        message(FATAL_ERROR
                "generate_python_bridge_plugins: pybind11::embed target not found. "
                "Call find_package(pybind11 REQUIRED) before including "
                "python/bridge.cmake.")
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
    # 3. Tier 1 — Profile yaml generation (timestamp-gated)
    # -----------------------------------------------------------------------
    file(TIMESTAMP "${_PY_BRIDGE_MASTER_PROFILE}"
         _py_bridge_master_ts "%s" UTC)

    if(NOT DEFINED CACHE{LAST_PY_BRIDGE_YAML_BUILD})
        set(LAST_PY_BRIDGE_YAML_BUILD "0" CACHE INTERNAL "")
    endif()

    if($CACHE{LAST_PY_BRIDGE_YAML_BUILD} LESS "${_py_bridge_master_ts}")
        message(STATUS "Rebuilding python bridge profile yaml files")
        file(MAKE_DIRECTORY "${_PY_BRIDGE_PROFILES_DIR}")

        execute_process(
                COMMAND "${Python3_EXECUTABLE}"
                "${_PY_BRIDGE_GENERATOR}"
                --write-profiles
                "${_PY_BRIDGE_MASTER_PROFILE}"
                "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE _tier1_out
                ERROR_VARIABLE  _tier1_err
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

        string(TIMESTAMP _now "%s" UTC)
        set(LAST_PY_BRIDGE_YAML_BUILD "${_now}"
            CACHE INTERNAL
            "Epoch timestamp of last python_bridges.yaml processing")

        message(STATUS
                "Python bridge profile yaml files written to "
                "${_PY_BRIDGE_BRIDGES_DIR}")
    else()
        message(STATUS "Python bridge profile yaml files are up-to-date")
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
    # 4. Tier 2 — Struct headers + plugin sources (always runs)
    # -----------------------------------------------------------------------
    execute_process(
            COMMAND "${Python3_EXECUTABLE}"
            "${_PY_BRIDGE_GENERATOR}"
            --generate
            "${_bridge_profile}"
            "${CMAKE_BINARY_DIR}"
            "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _tier2_out
            ERROR_VARIABLE  _tier2_err
            RESULT_VARIABLE _tier2_result
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT _tier2_result EQUAL 0)
        message(FATAL_ERROR
                "generate_python_bridge_plugins: code generation failed:\n"
                "${_tier2_err}\n${_tier2_out}")
    endif()
    if(_tier2_err)
        message(WARNING "${_tier2_err}")
    endif()

    cmake_language(EVAL CODE "${_tier2_out}")

    # -----------------------------------------------------------------------
    # 5. Validate generator output
    # -----------------------------------------------------------------------
    foreach(_required_var
            PY_BRIDGE_NAMES PY_BRIDGE_PLUGIN_DIRS PY_BRIDGE_PLUGIN_CPPS
            PY_BRIDGE_STRUCT_INCLUDE_DIR PY_BRIDGE_COUNT)
        if(NOT DEFINED ${_required_var})
            message(FATAL_ERROR
                    "generate_python_bridge_plugins: generator did not set "
                    "${_required_var}")
        endif()
    endforeach()

    # -----------------------------------------------------------------------
    # 6. Collect system binding sources (shared across all bridge targets)
    # -----------------------------------------------------------------------
    _py_bridge_collect_system_sources()
    # _PY_SYSTEM_SOURCES is now set in this scope

    # -----------------------------------------------------------------------
    # 7. Register each bridge plugin target
    # -----------------------------------------------------------------------
    list(LENGTH PY_BRIDGE_NAMES _n_bridges)
    if(_n_bridges EQUAL 0)
        return()
    endif()
    math(EXPR _last_idx "${_n_bridges} - 1")

    foreach(_idx RANGE ${_last_idx})
        list(GET PY_BRIDGE_NAMES       ${_idx} _bname)
        list(GET PY_BRIDGE_PLUGIN_DIRS ${_idx} _plugin_dir)
        list(GET PY_BRIDGE_PLUGIN_CPPS ${_idx} _plugin_cpp)
        list(GET PY_BRIDGE_HAS_NETWORK ${_idx} _has_net)

        # Bridge-specific sources: plugin.cpp + bindings_<type>.cpp
        file(GLOB _bridge_binding_files "${_plugin_dir}/bindings_*.cpp")
        set(_all_sources
            "${_plugin_cpp}"
            ${_bridge_binding_files}
            ${_PY_SYSTEM_SOURCES}   # system binding cpp + serialization cpp
        )

        add_illixr_plugin(${_bname}
                          SOURCES ${_all_sources}
        )

        # Include paths:
        #   1. Generated plugin dir  (generated plugin.hpp)
        #   2. ${CMAKE_BINARY_DIR}/include  (illixr/bridge/*.hpp)
        #   3. ${CMAKE_SOURCE_DIR}/include  (illixr/data_format/*.hpp for
        #      system binding headers)
        #   4. System bindings dir  (for any local headers)
        target_include_directories(plugin.${_bname} PRIVATE
                                   "${_plugin_dir}"
                                   "${PY_BRIDGE_STRUCT_INCLUDE_DIR}"
                                   "${CMAKE_SOURCE_DIR}/include"
                                   "${_PY_SYSTEM_BINDINGS_DIR}"
        )

        target_link_libraries(plugin.${_bname} PRIVATE
                              pybind11::embed
                              spdlog::spdlog
        )

        if(_has_net STREQUAL "TRUE")
            if(NOT TARGET Boost::serialization)
                message(FATAL_ERROR
                        "Bridge '${_bname}' has networked output topics but "
                        "Boost::serialization was not found.")
            endif()
            target_link_libraries(plugin.${_bname} PRIVATE
                                  Boost::serialization)
        endif()

        if(TARGET opencv_core)
            target_link_libraries(plugin.${_bname} PRIVATE opencv_core)
        elseif(OpenCV_FOUND)
            target_link_libraries(plugin.${_bname} PRIVATE ${OpenCV_LIBS})
        endif()

        # Per-bridge serialization compile definitions
        foreach(_ser_entry ${PY_BRIDGE_SERIALIZE_DEFS})
            string(REGEX MATCH "^([^:]+):(.*)" _ser_match "${_ser_entry}")
            if(CMAKE_MATCH_1 STREQUAL _bname AND CMAKE_MATCH_2)
                string(REPLACE ";" ";" _ser_defines "${CMAKE_MATCH_2}")
                foreach(_def ${_ser_defines})
                    target_compile_definitions(plugin.${_bname}
                                               PRIVATE "${_def}")
                endforeach()
            endif()
        endforeach()

        # -------------------------------------------------------------------
        # 8. Append to PLUGIN_UNORDERED → included in illixr.yaml
        # -------------------------------------------------------------------
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
