# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
#
# csharp_bridge.cmake
#
# Provides generate_csharp_bridge_plugins(), the C#/Unity counterpart to
# generate_python_bridge_plugins() in bridge.cmake. Structure is intentionally
# near-identical: same two-tier profile/generate split, same PLUGIN_UNORDERED
# registration, same CMAKE_CONFIGURE_DEPENDS tracking.
#
# Behaviour
# ---------
#   If -DCSHARP_BRIDGE_PROFILE is not set: returns immediately, nothing done.
#
#   If -DCSHARP_BRIDGE_PROFILE=<name>.yaml is set:
#     - Sets BUILD_UNITY_INTERFACE (a plain variable, not cached) so the
#       rest of this configure run knows to compile the native Unity
#       P/Invoke surface. Recomputed every configure -- switching to a
#       profile without a csharp bridge and re-running cmake will not
#       leave this stuck on.
#     - Tier 1 -- profile yaml generation (timestamp-gated on csharp_profiles.yaml)
#     - Tier 2 -- shared struct headers (interfaces/data/*.yaml, same output
#       as the Python generator produces -- reused as-is if this build also
#       ran generate_python_bridge_plugins()) plus per-bridge plugin sources:
#         ${CMAKE_BINARY_DIR}/generated/unity_plugins/<bridge>/plugin.hpp
#         ${CMAKE_BINARY_DIR}/generated/unity_plugins/<bridge>/plugin.cpp
#         ${CMAKE_BINARY_DIR}/generated/unity_plugins/<bridge>/unity_wire_<type>.hpp
#         ${CMAKE_BINARY_DIR}/generated/unity_plugins/<bridge>/CMakeLists.txt
#     - Each bridge is added via add_subdirectory and appended to
#       PLUGIN_UNORDERED, exactly like a Python bridge plugin, so it is
#       built and included in illixr.yaml the same way.
#
# -DCSHARP_BRIDGE_PROFILE interpretation
#   Filename only, .yaml extension required.
#   Resolved as: ${CMAKE_SOURCE_DIR}/interfaces/csharp/profiles/<value>
#   Bridge descriptor YAMLs live in interfaces/csharp/bridges/.
#   Generated per-profile YAMLs live in interfaces/csharp/profiles/.
#
# Placement in CMakeLists.txt
#   Call generate_csharp_bridge_plugins() alongside generate_python_bridge_plugins()
#   -- after generate_yaml(), check_plugins(), and read_yaml(), but before the
#   plugin add_subdirectory loop. Order relative to the Python call does not
#   matter; both only append to PLUGIN_UNORDERED.

cmake_minimum_required(VERSION 3.22)
find_package(Python COMPONENTS Interpreter Development REQUIRED)

if(DEFINED _ILLIXR_CSHARP_BRIDGE_INCLUDED)
    return()
endif()
set(_ILLIXR_CSHARP_BRIDGE_INCLUDED TRUE)

set(_CS_BRIDGE_GENERATOR
    "${CMAKE_SOURCE_DIR}/interfaces/csharp/generate_csharp_bridges.py"
    CACHE INTERNAL "Path to generate_csharp_bridges.py")

set(_CS_BRIDGE_MASTER_PROFILE
    "${CMAKE_SOURCE_DIR}/interfaces/csharp/csharp_profiles.yaml"
    CACHE INTERNAL "Path to master csharp bridge profile")

set(_CS_BRIDGE_BRIDGES_DIR
    "${CMAKE_SOURCE_DIR}/interfaces/csharp/bridges"
    CACHE INTERNAL "Directory containing C# bridge descriptor YAMLs")

set(_CS_BRIDGE_PROFILES_DIR
    "${CMAKE_SOURCE_DIR}/interfaces/csharp/profiles"
    CACHE INTERNAL "Directory containing generated per-profile YAMLs")

# ---------------------------------------------------------------------------
# generate_csharp_bridge_plugins()
# ---------------------------------------------------------------------------
function(generate_csharp_bridge_plugins)

    # -----------------------------------------------------------------------
    # 0. Nothing to do if no profile selected
    # -----------------------------------------------------------------------
    if(NOT CSHARP_BRIDGE_PROFILE)
        return()
    endif()

    # -----------------------------------------------------------------------
    # 1. Using the C# generator implies the native Unity interface must be
    #    built. This is intentionally NOT a cache variable: CACHE ... FORCE
    #    would persist ON in CMakeCache.txt even after a later configure
    #    drops -DCSHARP_BRIDGE_PROFILE, since nothing would ever set it back
    #    off. A plain PARENT_SCOPE variable is recomputed from scratch on
    #    every configure -- it is ON only for runs where this branch actually
    #    executes, and reverts to whatever BUILD_UNITY_INTERFACE's own
    #    default/option() value is otherwise, with no stale leftover state.
    #    This does mean the call site must happen before anything that reads
    #    BUILD_UNITY_INTERFACE (e.g. before add_subdirectory(plugins)), same
    #    placement requirement noted at the top of this file.
    # -----------------------------------------------------------------------
    set(BUILD_UNITY_INTERFACE ON PARENT_SCOPE)

    # -----------------------------------------------------------------------
    # 2. Resolve the bridge profile path
    # -----------------------------------------------------------------------
    set(_bridge_profile
        "${_CS_BRIDGE_PROFILES_DIR}/${CSHARP_BRIDGE_PROFILE}")

    # -----------------------------------------------------------------------
    # 3. Prerequisite checks
    # -----------------------------------------------------------------------
    if(NOT Python3_EXECUTABLE)
        message(FATAL_ERROR
                "generate_csharp_bridge_plugins: Python3 interpreter not found. "
                "Call find_package(Python3 REQUIRED COMPONENTS Interpreter) "
                "before including csharp_bridge.cmake.")
    endif()

    if(NOT EXISTS "${_CS_BRIDGE_GENERATOR}")
        message(FATAL_ERROR
                "generate_csharp_bridge_plugins: generator script not found: "
                "${_CS_BRIDGE_GENERATOR}")
    endif()

    if(NOT EXISTS "${_CS_BRIDGE_MASTER_PROFILE}")
        message(FATAL_ERROR
                "generate_csharp_bridge_plugins: master profile not found: "
                "${_CS_BRIDGE_MASTER_PROFILE}")
    endif()

    # -----------------------------------------------------------------------
    # 4. Tier 1 -- Profile yaml generation (timestamp-gated on master profile)
    # -----------------------------------------------------------------------
    file(MD5 "${_CS_BRIDGE_MASTER_PROFILE}" _cs_bridge_master_hash)

    if(NOT DEFINED CACHE{LAST_CS_PROFILES_YAML_BUILD})
        set(LAST_CS_PROFILES_YAML_BUILD "" CACHE INTERNAL "")
    endif()

    if(NOT "${LAST_CS_PROFILES_YAML_BUILD}" STREQUAL "${_cs_bridge_master_hash}")
        message(STATUS "Rebuilding C# bridge profile yaml files from csharp_profiles.yaml")
        file(MAKE_DIRECTORY "${_CS_BRIDGE_PROFILES_DIR}")

        execute_process(
                COMMAND "${Python3_EXECUTABLE}"
                "${_CS_BRIDGE_GENERATOR}"
                --write-profiles
                "${_CS_BRIDGE_MASTER_PROFILE}"
                "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE _tier1_out
                ERROR_VARIABLE _tier1_err
                RESULT_VARIABLE _tier1_result
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(NOT _tier1_result EQUAL 0)
            message(FATAL_ERROR
                    "generate_csharp_bridge_plugins: profile generation failed:\n"
                    "${_tier1_err}\n${_tier1_out}")
        endif()
        if(_tier1_err)
            message(WARNING "${_tier1_err}")
        endif()

        file(MD5 "${_CS_BRIDGE_MASTER_PROFILE}" _master_md5)
        set(LAST_CS_PROFILES_YAML_BUILD "${_master_md5}"
            CACHE INTERNAL "MD5 of csharp_profiles.yaml at last processing")
    endif()

    if(NOT EXISTS "${_bridge_profile}")
        message(FATAL_ERROR
                "CSHARP_BRIDGE_PROFILE '${CSHARP_BRIDGE_PROFILE}' was not found "
                "at ${_bridge_profile}.\n"
                "Generated profile YAMLs live in interfaces/csharp/profiles/.\n"
                "Available profiles are defined in "
                "${_CS_BRIDGE_MASTER_PROFILE}.")
    endif()

    # -----------------------------------------------------------------------
    # 5. Tier 2 -- Shared struct headers + Unity plugin sources
    #
    # No staleness/state-file gating yet (unlike the Python side) -- every
    # configure re-runs the generator in full. Fine for now; port the MD5
    # state-file caching from state.py once the codegen surface stabilizes.
    # -----------------------------------------------------------------------
    message(STATUS "Running C# bridge generator...")
    execute_process(
            COMMAND "${Python3_EXECUTABLE}"
            "${_CS_BRIDGE_GENERATOR}"
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
                "generate_csharp_bridge_plugins: code generation failed:\n"
                "${_tier2_err}\n${_tier2_out}")
    endif()

    cmake_language(EVAL CODE "${_tier2_out}")

    # -----------------------------------------------------------------------
    # 6. Validate required variables were emitted
    # -----------------------------------------------------------------------
    foreach(_required_var
            CS_BRIDGE_NAMES CS_BRIDGE_CMAKE_DIRS CS_BRIDGE_COUNT)
        if(NOT DEFINED ${_required_var})
            message(FATAL_ERROR
                    "generate_csharp_bridge_plugins: '${_required_var}' is not set "
                    "and no cached value exists. Delete CMakeCache.txt and re-run cmake.")
        endif()
    endforeach()

    # -----------------------------------------------------------------------
    # 7. Register each bridge plugin via add_subdirectory, and append it to
    #    PLUGIN_UNORDERED exactly like a Python bridge plugin, so it builds
    #    and is included in illixr.yaml the same way.
    # -----------------------------------------------------------------------
    list(LENGTH CS_BRIDGE_NAMES _n_bridges)
    if(_n_bridges EQUAL 0)
        return()
    endif()
    math(EXPR _last_idx "${_n_bridges} - 1")

    foreach(_idx RANGE ${_last_idx})
        list(GET CS_BRIDGE_NAMES ${_idx} _bname)
        list(GET CS_BRIDGE_CMAKE_DIRS ${_idx} _cmake_dir)

        add_subdirectory("${_cmake_dir}" "${CMAKE_BINARY_DIR}/bridge_build/${_bname}")

        if(PLUGIN_UNORDERED)
            set(PLUGIN_UNORDERED "${PLUGIN_UNORDERED},${_bname}"
                PARENT_SCOPE)
        else()
            set(PLUGIN_UNORDERED "${_bname}" PARENT_SCOPE)
        endif()

        message(STATUS "C# bridge plugin '${_bname}' configured")
    endforeach()

    # -----------------------------------------------------------------------
    # 7b. Stash delivery instructions for ConfigurationSummary.cmake to print
    #     at the end of configuration.
    #
    #     ConfigurationSummary.cmake is a plain top-to-bottom script (not a
    #     registration API) that runs in its own include scope, so CACHE
    #     INTERNAL is what actually gets this list out of this function's
    #     scope and into that script's -- a PARENT_SCOPE variable would not
    #     survive the trip. See the new block in ConfigurationSummary.cmake
    #     printing ILLIXR_UNITY_SUMMARY, styled to match the existing
    #     "External Libraries"/"Build Plugins" sections.
    # -----------------------------------------------------------------------
    string(REPLACE ";" ", " _cs_bridge_names_joined "${CS_BRIDGE_NAMES}")

    set(_cs_summary
        "  Profile           : ${CSHARP_BRIDGE_PROFILE}"
        "  Bridges generated : ${_cs_bridge_names_joined}"
        ""
        "  Generated C# scripts:"
        "    ${CMAKE_BINARY_DIR}/generated/unity_csharp/"
        "    -> Copy these .cs files into your Unity project's"
        "       Assets/Scripts/ILLIXR/ directory."
        ""
        "  Built native plugin libraries:"
        "    Android (default Gradle/CMake external-native-build layout):"
        "      ${CMAKE_SOURCE_DIR}/app/build/intermediates/cxx/<build_type>/<hash>/obj/<ABI?/"
        "      NOTE: this is NOT under this project's own build directory"
        "      (${CMAKE_BINARY_DIR}) -- CMake's own working directory lives"
        "      under app/.cxx/, a sibling of app/build/. The intermediates/cxx"
        "      tree is Gradle's separate packaging output for the app module."
        "      <build_type> is 'Debug' or 'Release' (capitalized)."
        "      <hash> is assigned by Gradle per build and varies -- locate it with:"
        "        find ${CMAKE_SOURCE_DIR}/app/build/intermediates/cxx -name 'lib*.so'"
        "      -> Copy the .so into"
        "         Assets/Plugins/Android/arm64-v8a/ in your Unity project."
        "    Linux/Windows:"
        "      ${CMAKE_BINARY_DIR} and its subdirectories"
        "      (look for lib<plugin-name>.so or <plugin-name>.dll)"
        "      -> Copy the built library into"
        "         Assets/Plugins/<x86_64|Win64>/ in your Unity project."
    )

    set(ILLIXR_UNITY_SUMMARY "${_cs_summary}" CACHE INTERNAL
        "Unity/C# bridge delivery instructions, printed by ConfigurationSummary.cmake")

    # -----------------------------------------------------------------------
    # 8. Re-configure triggers
    # -----------------------------------------------------------------------
    set_property(DIRECTORY APPEND PROPERTY
                 CMAKE_CONFIGURE_DEPENDS "${_CS_BRIDGE_MASTER_PROFILE}")
    set_property(DIRECTORY APPEND PROPERTY
                 CMAKE_CONFIGURE_DEPENDS "${_bridge_profile}")

    file(STRINGS "${_bridge_profile}" _bp_content)
    foreach(_line ${_bp_content})
        string(REGEX MATCH "bridges:[ \t]*(.*)" _m "${_line}")
        if(CMAKE_MATCH_1)
            string(REPLACE "," ";" _bnames "${CMAKE_MATCH_1}")
            foreach(_bn ${_bnames})
                string(STRIP "${_bn}" _bn)
                set(_bdesc "${_CS_BRIDGE_BRIDGES_DIR}/${_bn}.yaml")
                if(EXISTS "${_bdesc}")
                    set_property(DIRECTORY APPEND PROPERTY
                                 CMAKE_CONFIGURE_DEPENDS "${_bdesc}")
                endif()
            endforeach()
        endif()
    endforeach()

endfunction()
