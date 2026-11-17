# Mosaic — top-level CMake project
# See CLAUDE.md for the rules, docs/SPEC.md for behavior, docs/BUILD_PLAN.md for milestones.

cmake_minimum_required(VERSION 3.24)

# Universal binary + deployment target must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Mosaic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MOSAIC_BUILD_TESTS "Build the Catch2 unit tests" ON)
option(MOSAIC_COPY_PLUGIN "Copy built plugins into the user plugin folders" ON)

# ---------------------------------------------------------------------------
# Dependencies (git submodules, pinned — see THIRD_PARTY_LICENSES.md)
# ---------------------------------------------------------------------------
add_subdirectory(libs/JUCE)

# ---------------------------------------------------------------------------
# Engine: plain C++, no JUCE plugin modules. Must build and test on its own.
# ---------------------------------------------------------------------------
add_library(mosaic_engine STATIC
    source/engine/EngineInfo.cpp
)
target_include_directories(mosaic_engine PUBLIC source)
target_compile_features(mosaic_engine PUBLIC cxx_std_20)
target_compile_options(mosaic_engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(Mosaic
    PRODUCT_NAME                "Mosaic"
    COMPANY_NAME                "Richardson Media Solutions"
    COMPANY_WEBSITE             "https://richardsonmediasolutions.com"
    BUNDLE_ID                   "com.richardsonmediasolutions.mosaic"
    PLUGIN_MANUFACTURER_CODE    Rmsl
    PLUGIN_CODE                 Mosc
    FORMATS                     AU VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx
    VST3_AUTO_MANIFEST          FALSE
    COPY_PLUGIN_AFTER_BUILD     ${MOSAIC_COPY_PLUGIN}
    HARDENED_RUNTIME_ENABLED    TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "Mosaic needs audio input for its Standalone app."
)

target_sources(Mosaic PRIVATE
    source/plugin/PluginProcessor.cpp
    source/plugin/PluginEditor.cpp
)

target_include_directories(Mosaic PRIVATE source)

target_compile_definitions(Mosaic PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    MOSAIC_VERSION_STRING="${PROJECT_VERSION}"
)

target_compile_options(Mosaic PRIVATE -Wall -Wextra -Wshadow)

target_link_libraries(Mosaic
    PRIVATE
        mosaic_engine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MOSAIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(libs/Catch2)
    add_subdirectory(tests)
endif()
