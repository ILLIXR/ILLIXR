# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
#
# python/bridge.cmake
#
# Provides generate_python_bridge_plugins() which integrates with the
# existing ILLIXR cmake infrastructure.
#
# Behaviour
# ---------
#   If -DPYTHON_BRIDGE_PROFILE is not set:  returns immediately, nothing done.
#
#   If -DPYTHON_BRIDGE_PROFILE=<name>.yaml is set:
#
#     Tier 1 — Profile yaml generation (timestamp-gated)
#       Mirrors the generate_yaml() pattern in HelperFunctions.cmake.
#       Reads interfaces/python/python_bridges.yaml and writes one
#       interfaces/python/bridges/<profile>.yaml per profile entry.
#       Only runs when python_bridges.yaml has changed since the last
#       configure (tracked via LAST_PY_BRIDGE_YAML_BUILD cache variable)
#       or when the cache is absent.
#
#     Tier 2 — Struct headers + plugin sources (always runs)
#       Generates for every bridge listed in the selected profile:
#         ${CMAKE_BINARY_DIR}/include/illixr/bridge/<type>.hpp
#         ${CMAKE_BINARY_DIR}/generated/plugins/<bridge>/plugin.hpp
#         ${CMAKE_BINARY_DIR}/generated/plugins/<bridge>/plugin.cpp
#         ${CMAKE_BINARY_DIR}/generated/plugins/<bridge>/bindings_<type>.cpp
#       Registers each bridge as an ILLIXR plugin target via add_illixr_plugin().
#       Appends each bridge name to PLUGIN_UNORDERED in the parent scope so
#       ConfigurationSummary.cmake includes them in illixr.yaml.
#
# -DPYTHON_BRIDGE_PROFILE interpretation
#   The value is treated as a filename only (no path, no directory).
#   The .yaml extension is required.
#   The file is looked up at:
#     ${CMAKE_SOURCE_DIR}/interfaces/python/bridges/<value>
#
# Placement in CMakeLists.txt
#   Call generate_python_bridge_plugins() AFTER generate_yaml(),
#   check_plugins(), and read_yaml() but BEFORE the plugin
#   add_subdirectory loop, so C++ plugins can reference bridge
#   headers as sources.
#
# Prerequisites
#   find_package(Python3 REQUIRED COMPONENTS Interpreter)
#   find_package(pybind11 REQUIRED)
#   find_package(spdlog   REQUIRED)
#   find_package(Boost    REQUIRED COMPONENTS serialization)  # if any network outputs
#   # OpenCV linked automatically if found

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

# ---------------------------------------------------------------------------
# generate_python_bridge_plugins()
# ---------------------------------------------------------------------------
function(generate_python_bridge_plugins)

    # -----------------------------------------------------------------------
    # 1. Resolve the bridge profile path
    #    Accept the bare filename; prepend the bridges directory.
    # -----------------------------------------------------------------------
    set(_bridge_profile
        "${_PY_BRIDGE_BRIDGES_DIR}/${PYTHON_BRIDGE_PROFILE}")

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
    #    Mirrors the LAST_YAML_BUILD pattern in HelperFunctions.cmake.
    # -----------------------------------------------------------------------
    file(TIMESTAMP "${_PY_BRIDGE_MASTER_PROFILE}"
         _py_bridge_master_ts "%s" UTC)

    if(NOT DEFINED CACHE{LAST_PY_BRIDGE_YAML_BUILD})
        set(LAST_PY_BRIDGE_YAML_BUILD "0" CACHE INTERNAL "")
    endif()

    if($CACHE{LAST_PY_BRIDGE_YAML_BUILD} LESS "${_py_bridge_master_ts}")
        message(STATUS "Rebuilding python bridge profile yaml files")

        # Ensure the bridges output directory exists
        file(MAKE_DIRECTORY "${_PY_BRIDGE_BRIDGES_DIR}")

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
            message(WARNING
                "generate_python_bridge_plugins: profile generation warnings:"
                "\n${_tier1_err}")
        endif()

        # Update cached timestamp
        string(TIMESTAMP _now "%s" UTC)
        set(LAST_PY_BRIDGE_YAML_BUILD "${_now}"
            CACHE INTERNAL "Epoch timestamp of last python_bridges.yaml processing")

        message(STATUS "Python bridge profile yaml files written to "
                       "${_PY_BRIDGE_BRIDGES_DIR}")
    else()
        message(STATUS "Python bridge profile yaml files are up-to-date")
    endif()

    # Now that profile yamls are guaranteed to exist, verify the selected one
    if(NOT EXISTS "${_bridge_profile}")
        message(FATAL_ERROR
            "PYTHON_BRIDGE_PROFILE '${PYTHON_BRIDGE_PROFILE}' was not found "
            "at ${_bridge_profile}.\n"
            "Available profiles are listed in "
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
        message(WARNING
            "generate_python_bridge_plugins: code generation warnings:\n"
            "${_tier2_err}")
    endif()

    # Bring all PY_BRIDGE_* variables into scope
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
    # 6. Register each bridge as an ILLIXR plugin target
    # -----------------------------------------------------------------------
    list(LENGTH PY_BRIDGE_NAMES _n_bridges)
    if(_n_bridges EQUAL 0)
        return()
    endif()
    math(EXPR _last_idx "${_n_bridges} - 1")

    foreach(_idx RANGE ${_last_idx})
        list(GET PY_BRIDGE_NAMES      ${_idx} _bname)
        list(GET PY_BRIDGE_PLUGIN_DIRS ${_idx} _plugin_dir)
        list(GET PY_BRIDGE_PLUGIN_CPPS ${_idx} _plugin_cpp)
        list(GET PY_BRIDGE_HAS_NETWORK ${_idx} _has_net)

        # Collect all source files: plugin.cpp + bindings_*.cpp
        file(GLOB _binding_files "${_plugin_dir}/bindings_*.cpp")
        set(_all_sources "${_plugin_cpp}" ${_binding_files})

        # Register via the ILLIXR helper (creates plugin.<name> target,
        # sets PLUGIN_NAME macro, links illixr_plugin_base, etc.)
        add_illixr_plugin(${_bname}
            SOURCES ${_all_sources}
        )

        # Include paths:
        #   1. The generated plugin dir (for generated plugin.hpp)
        #   2. ${CMAKE_BINARY_DIR}/include (for illixr/bridge/*.hpp)
        #      — same pattern as ${CMAKE_SOURCE_DIR}/include for
        #        non-generated ILLIXR headers
        target_include_directories(plugin.${_bname} PRIVATE
            "${_plugin_dir}"
            "${PY_BRIDGE_STRUCT_INCLUDE_DIR}"
        )

        # Core link dependencies
        target_link_libraries(plugin.${_bname} PRIVATE
            pybind11::embed
            spdlog::spdlog
        )

        # Boost serialization for networked output topics
        if(_has_net STREQUAL "TRUE")
            if(NOT TARGET Boost::serialization)
                message(FATAL_ERROR
                    "Bridge '${_bname}' has networked output topics but "
                    "Boost::serialization was not found. Call "
                    "find_package(Boost REQUIRED COMPONENTS serialization).")
            endif()
            target_link_libraries(plugin.${_bname} PRIVATE
                Boost::serialization)
        endif()

        # OpenCV — link if available (needed for mat_* fields)
        if(TARGET opencv_core)
            target_link_libraries(plugin.${_bname} PRIVATE opencv_core)
        elseif(OpenCV_FOUND)
            target_link_libraries(plugin.${_bname} PRIVATE ${OpenCV_LIBS})
        endif()

        # Per-bridge serialization compile definitions
        # PY_BRIDGE_SERIALIZE_DEFS entries are "bridgename:DEF1;DEF2"
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
        # 7. Add to PLUGIN_UNORDERED in the parent scope so
        #    ConfigurationSummary.cmake includes the plugin in illixr.yaml.
        #    Mirrors the append pattern used in the main plugin loop.
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
    # 8. Re-configure if input files change
    # -----------------------------------------------------------------------
    set_property(DIRECTORY APPEND PROPERTY
        CMAKE_CONFIGURE_DEPENDS "${_PY_BRIDGE_MASTER_PROFILE}")
    set_property(DIRECTORY APPEND PROPERTY
        CMAKE_CONFIGURE_DEPENDS "${_bridge_profile}")

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
                        # Match bare type name entries under 'types:'
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
