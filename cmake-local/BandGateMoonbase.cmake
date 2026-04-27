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
    set(MB_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/moonbase_JUCEClient")

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

    # Add moonbase sources directly to the target
    target_sources(${MB_TARGET} PRIVATE "${MB_DEST_DIR}/moonbase_JUCEClient.cpp")

    # IMPORTANT: Add the build dir BEFORE any other include paths
    # so that headers from the build copy are found first (not the source module).
    target_include_directories(${MB_TARGET} BEFORE PRIVATE "${MB_DEST_DIR}")

    target_compile_definitions(${MB_TARGET} PRIVATE
        JUCE_MODULE_AVAILABLE_moonbase_JUCEClient=1
        INCLUDE_MOONBASE_UI=0)

    # Moonbase module dependencies
    target_link_libraries(${MB_TARGET} PRIVATE
        juce_product_unlocking)

endfunction()
