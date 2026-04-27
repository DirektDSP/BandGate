cmake_minimum_required(VERSION 3.25)

# Pamplejuce cmake helpers (submodule) and our own cmake-local
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake-local")
include(PamplejuceVersion)

# Modern concise way to add dependencies to your project
include(CPM)

# Configures universal binaries and decides which version of macOS to support
include(PamplejuceMacOS)

# Couple tweaks that IMO should be JUCE defaults
include(JUCEDefaults)

set(PROJECT_NAME "BandGate")
set(PRODUCT_NAME "BandGate")
set(COMPANY_NAME "DirektDSP")
set(BUNDLE_ID "com.direktdsp.bandgate")

# Valid choices: AAX Unity VST VST3 AU AUv3 Standalone CLAP
set(FORMATS AU VST3 AUv3 CLAP Standalone)

project(${PROJECT_NAME} VERSION ${CURRENT_VERSION})

# JUCE is setup as a submodule in the /JUCE folder
add_subdirectory(JUCE)

# Shared modules
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)
add_subdirectory(modules/melatonin_inspector)
add_subdirectory(modules/DirektDSP_GUI)

# Moonbase setup helper (handles disable flag + stubs)
include(BandGateMoonbase)

# See `docs/CMake API.md` in the JUCE repo for all config options
juce_add_plugin("${PROJECT_NAME}"
    # Icons for the standalone app
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png"

    COMPANY_NAME "${COMPANY_NAME}"
    BUNDLE_ID "${BUNDLE_ID}"

    # On MacOS, plugin is copied to /Users/yourname/Library/Audio/Plug-Ins/
    COPY_PLUGIN_AFTER_BUILD TRUE

    # A four-character plugin id
    # First character MUST be uppercase for AU formats
    PLUGIN_MANUFACTURER_CODE Manu

    # A unique four-character plugin id
    # Note: this must have at least one upper-case character
    PLUGIN_CODE Bgt1
    FORMATS "${FORMATS}"

    # The name of your final executable
    # This is how it's listed in the DAW
    PRODUCT_NAME "${PRODUCT_NAME}")

# This lets us use our code in both the JUCE targets and our Test target
# Without running into ODR violations
add_library(SharedCode INTERFACE)

clap_juce_extensions_plugin(TARGET "${PROJECT_NAME}"
    CLAP_ID "${BUNDLE_ID}"
    CLAP_FEATURES audio-effect)

# Per-plugin moonbase config generation + linking
bandgate_setup_moonbase(
    TARGET "${PROJECT_NAME}"
    CONFIG_JSON "moonbase_api_config.json")

# Compiler options
if (MSVC)
    target_compile_options(SharedCode INTERFACE $<$<CONFIG:RELEASE>:/fp:fast>)
    target_compile_options(SharedCode INTERFACE $<$<CONFIG:RELEASE>:/Ox>)
    target_compile_options(SharedCode INTERFACE /Zc:__cplusplus)
else ()
    target_compile_options(SharedCode INTERFACE $<$<CONFIG:RELEASE>:-Ofast>)
    target_compile_options(SharedCode INTERFACE $<$<CONFIG:RelWithDebInfo>:-Ofast>)
endif ()
target_compile_features(SharedCode INTERFACE cxx_std_20)

file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")
target_include_directories(SharedCode INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/source")
target_sources(SharedCode INTERFACE ${SourceFiles})

# Adds a BinaryData target for embedding assets into the binary
file(GLOB_RECURSE AssetFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
list(FILTER AssetFiles EXCLUDE REGEX "/\\.DS_Store$")
if (AssetFiles)
    juce_add_binary_data(${PROJECT_NAME}_Assets SOURCES ${AssetFiles})
    set_target_properties(${PROJECT_NAME}_Assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    target_link_libraries(SharedCode INTERFACE ${PROJECT_NAME}_Assets)
endif()

# MacOS only: Cleans up folder and target organization on Xcode.
include(XcodePrettify)

# This is where you can set preprocessor definitions for JUCE and your plugin
target_compile_definitions(SharedCode
    INTERFACE

    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0

    # lets the app known if we're Debug or Release
    CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    VERSION="${CURRENT_VERSION}"

    PRODUCT_NAME_WITHOUT_VERSION="BandGate"
)

target_link_libraries(SharedCode
    INTERFACE
    DirektDSP_GUI
    melatonin_inspector
    juce_audio_utils
    juce_audio_processors
    juce_dsp
    juce_gui_basics
    juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Link the JUCE plugin target to our SharedCode target
target_link_libraries("${PROJECT_NAME}" PRIVATE SharedCode)

# IPP support, comment out to disable
include(PamplejuceIPP)

# Everything related to the tests target
include(Tests)

# A separate target for Benchmarks (keeps the Tests target fast)
include(Benchmarks)

# Output some config for CI (like our PRODUCT_NAME)
include(GitHubENV)
