# BandGateMoonbase.cmake
# Sets up moonbase_JUCEClient with per-plugin config generation.
#
# Set BANDGATE_DISABLE_MOONBASE=ON to build without licensing (dev builds).
#
# Usage:
#   bandgate_setup_moonbase(
#       TARGET      <target_name>
#       CONFIG_JSON <path_to_moonbase_api_config.json>
#   )

option(BANDGATE_DISABLE_MOONBASE "Disable Moonbase licensing for dev builds" OFF)

function(bandgate_setup_moonbase)
    cmake_parse_arguments(MB "" "TARGET;CONFIG_JSON" "" ${ARGN})

    if(NOT MB_TARGET OR NOT MB_CONFIG_JSON)
        message(FATAL_ERROR "bandgate_setup_moonbase requires TARGET and CONFIG_JSON")
    endif()

    if(BANDGATE_DISABLE_MOONBASE)
        message(STATUS "Moonbase DISABLED for ${MB_TARGET} (dev build)")
        target_compile_definitions(${MB_TARGET} PRIVATE
            BANDGATE_NO_MOONBASE=1)
        # Add stubs header to include path
        target_include_directories(${MB_TARGET} PRIVATE
            "${CMAKE_SOURCE_DIR}/cmake-local")
        return()
    endif()

    set(MB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/modules/moonbase_JUCEClient")
    # Keep moonbase include root isolated from full build dir so C++ stdlib headers
    # like <version> cannot be shadowed by generated files in ${CMAKE_BINARY_DIR}.
    set(MB_INCLUDE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/moonbase_include_root")
    set(MB_DEST_DIR "${MB_INCLUDE_ROOT}/moonbase_JUCEClient")

    # Copy moonbase module to build directory
    file(COPY "${MB_SOURCE_DIR}/"
        DESTINATION "${MB_DEST_DIR}"
        PATTERN ".git" EXCLUDE)

    # Run PreBuild.sh with this plugin's config
    if(NOT EXISTS "${MB_DEST_DIR}/PreBuild.sh")
        message(FATAL_ERROR "PreBuild.sh not found at ${MB_DEST_DIR}/PreBuild.sh")
    endif()

    # Convert to absolute path if relative
    if(NOT IS_ABSOLUTE "${MB_CONFIG_JSON}")
        set(MB_CONFIG_JSON "${CMAKE_SOURCE_DIR}/assets/${MB_CONFIG_JSON}")
    endif()

    if(NOT EXISTS "${MB_CONFIG_JSON}")
        message(FATAL_ERROR "Config JSON not found at ${MB_CONFIG_JSON}")
    endif()

    execute_process(
        COMMAND bash "${MB_DEST_DIR}/PreBuild.sh" "${MB_CONFIG_JSON}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE MB_RESULT
        OUTPUT_VARIABLE MB_OUTPUT
        ERROR_VARIABLE MB_ERROR)

    if(NOT MB_RESULT EQUAL 0)
        message(FATAL_ERROR "moonbase PreBuild.sh failed for ${MB_TARGET}\nSTDOUT: ${MB_OUTPUT}\nSTDERR: ${MB_ERROR}")
    endif()

    # MSVC C1014 (include depth 1024): even a TU that only pulls BinaryIncludes.cpp -> MoonbaseBinary1.cpp
    # can hit the limit. Do not compile BinaryIncludes.cpp on MSVC; compile each MoonbaseBinary*.cpp as its
    # own translation unit (same symbols, shallow #include stack). Upstream moonbase sources stay unedited.
    if(MSVC)
        set(_mb_strip "    #include \"Assets/BinaryIncludes.cpp\"")
        file(READ "${MB_DEST_DIR}/moonbase_JUCEClient.cpp" _mb_client_src)
        string(FIND "${_mb_client_src}" "${_mb_strip}" _mb_strip_pos)
        if(_mb_strip_pos LESS 0)
            message(FATAL_ERROR "BandGate MSVC moonbase workaround: '${_mb_strip}' not found in moonbase_JUCEClient.cpp")
        endif()
        string(REPLACE "\n${_mb_strip}\n" "\n" _mb_patched "${_mb_client_src}")
        string(REPLACE "\r\n${_mb_strip}\r\n" "\r\n" _mb_patched "${_mb_patched}")
        file(WRITE "${MB_DEST_DIR}/moonbase_JUCEClient_MSVC.cpp" "${_mb_patched}")

        file(GLOB MB_ASSET_CPPS "${MB_DEST_DIR}/Assets/MoonbaseBinary*.cpp")
        list(LENGTH MB_ASSET_CPPS _mb_asset_count)
        if(_mb_asset_count LESS 1)
            message(FATAL_ERROR "BandGate MSVC moonbase workaround: no Assets/MoonbaseBinary*.cpp under ${MB_DEST_DIR}")
        endif()

        target_sources(${MB_TARGET} PRIVATE
            ${MB_ASSET_CPPS}
            "${MB_DEST_DIR}/moonbase_JUCEClient_MSVC.cpp")
    else()
        target_sources(${MB_TARGET} PRIVATE "${MB_DEST_DIR}/moonbase_JUCEClient.cpp")
    endif()

    # IMPORTANT: Add build-copy dirs BEFORE any other include paths so headers from
    # generated moonbase copy are found first (not source module).
    target_include_directories(${MB_TARGET} BEFORE PRIVATE "${MB_DEST_DIR}")
    # <moonbase_JUCEClient/...> includes must resolve to the *build* copy (PreBuild), not
    # modules/moonbase_JUCEClient, or MoonbaseBinary.h is parsed twice and const defs ODR-fail.
    target_include_directories(${MB_TARGET} BEFORE PRIVATE "${MB_INCLUDE_ROOT}")

    target_compile_definitions(${MB_TARGET} PRIVATE
        JUCE_MODULE_AVAILABLE_moonbase_JUCEClient=1
        INCLUDE_MOONBASE_UI=1)

    # Moonbase module dependencies
    target_link_libraries(${MB_TARGET} PRIVATE
        juce_product_unlocking)

endfunction()
