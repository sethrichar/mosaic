# Mosaic — top-level CMake project
# See CLAUDE.md for the rules, docs/SPEC.md for behavior, docs/BUILD_PLAN.md for milestones.

cmake_minimum_required(VERSION 3.24)

# Universal binary + deployment target must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Mosaic VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MOSAIC_BUILD_TESTS "Build the Catch2 unit tests" ON)
option(MOSAIC_COPY_PLUGIN "Copy built plugins into the user plugin folders" ON)
option(MOSAIC_BUILD_INST_PROBE "Build the M1 instrument-variant probe plugin" ON)

# ---------------------------------------------------------------------------
# Dependencies (git submodules, pinned — see THIRD_PARTY_LICENSES.md)
# ---------------------------------------------------------------------------
add_subdirectory(libs/JUCE)
# Pure C++ FFT, not Accelerate: renders must be bit-identical regardless of buffer
# alignment or thread (SPEC 7.6, section 11). Offline only, so speed is not a concern.
set(SIGNALSMITH_USE_ACCELERATE OFF CACHE BOOL "Mosaic: deterministic pure-C++ FFT" FORCE)
add_subdirectory(libs/signalsmith-linear)    # pinned 0.6.4; defines target signalsmith-linear
add_subdirectory(libs/signalsmith-stretch)   # pinned 1.4.0; uses the target above instead of FetchContent

set(MOSAIC_PLAIN_CXX_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

# ---------------------------------------------------------------------------
# Engine: plain C++, no JUCE plugin modules. Must build and test on its own.
# ---------------------------------------------------------------------------
add_library(mosaic_engine STATIC
    source/engine/EngineInfo.cpp
    source/engine/GridClock.cpp
    source/engine/SlicePosition.cpp
    source/engine/StepChooser.cpp
    source/engine/TileRenderer.cpp
)
target_include_directories(mosaic_engine PUBLIC source)
target_compile_features(mosaic_engine PUBLIC cxx_std_20)
target_compile_options(mosaic_engine PRIVATE ${MOSAIC_PLAIN_CXX_WARNINGS})

# ---------------------------------------------------------------------------
# Analysis: key detection and snap math. Plain C++, no plugin or UI dependencies.
# ---------------------------------------------------------------------------
add_library(mosaic_analysis STATIC
    source/analysis/KeyDetector.cpp
    source/analysis/KeySnap.cpp
    source/analysis/SignalsmithPitchShifter.cpp
)
target_include_directories(mosaic_analysis PUBLIC source)
target_compile_features(mosaic_analysis PUBLIC cxx_std_20)
target_compile_options(mosaic_analysis PRIVATE ${MOSAIC_PLAIN_CXX_WARNINGS})
target_link_libraries(mosaic_analysis PRIVATE signalsmith-stretch)
# The stretch header is heavy on templates; keep its warnings out of our -Wconversion build.
set_source_files_properties(source/analysis/SignalsmithPitchShifter.cpp PROPERTIES COMPILE_OPTIONS "-Wno-conversion;-Wno-shadow;-Wno-pedantic")

# ---------------------------------------------------------------------------
# Sources (plain C++ part): file slot source with lock-free buffer hand-off.
# ---------------------------------------------------------------------------
add_library(mosaic_sources STATIC
    source/sources/FileSlotSource.cpp
)
target_include_directories(mosaic_sources PUBLIC source)
target_compile_features(mosaic_sources PUBLIC cxx_std_20)
target_compile_options(mosaic_sources PRIVATE ${MOSAIC_PLAIN_CXX_WARNINGS})
target_link_libraries(mosaic_sources PUBLIC mosaic_engine)

# ---------------------------------------------------------------------------
# Send hub: plain C++, lock-free inter-instance transport (SPEC 5.3).
# ---------------------------------------------------------------------------
add_library(mosaic_hub STATIC
    source/hub/SendHub.cpp
)
target_include_directories(mosaic_hub PUBLIC source)
target_compile_features(mosaic_hub PUBLIC cxx_std_20)
target_compile_options(mosaic_hub PRIVATE ${MOSAIC_PLAIN_CXX_WARNINGS})

# ---------------------------------------------------------------------------
# Plugin sources shared by every plugin target and the plugin tests
# ---------------------------------------------------------------------------
set(MOSAIC_PLUGIN_SOURCES
    source/plugin/Parameters.cpp
    source/plugin/PluginProcessor.cpp
    source/plugin/PluginEditor.cpp
    source/sources/FileLoader.cpp
    source/ui/SlotRow.cpp
)

set(MOSAIC_PLUGIN_SOURCES_ABS "")
foreach(src ${MOSAIC_PLUGIN_SOURCES})
    list(APPEND MOSAIC_PLUGIN_SOURCES_ABS "${CMAKE_SOURCE_DIR}/${src}")
endforeach()

set(MOSAIC_JUCE_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    MOSAIC_VERSION_STRING="${PROJECT_VERSION}"
)

# One function so the shipping plugin and the M1 instrument probe stay identical
# apart from identity and category.
function(mosaic_add_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT_NAME;PLUGIN_CODE;BUNDLE_ID;IS_SYNTH;NEEDS_MIDI_INPUT;AU_MAIN_TYPE;VST3_CATEGORIES" "" ${ARGN})

    juce_add_plugin(${target}
        PRODUCT_NAME                "${ARG_PRODUCT_NAME}"
        COMPANY_NAME                "Richardson Media Solutions"
        COMPANY_WEBSITE             "https://richardsonmediasolutions.com"
        BUNDLE_ID                   "${ARG_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE    Rmsl
        PLUGIN_CODE                 ${ARG_PLUGIN_CODE}
        FORMATS                     AU VST3 Standalone
        IS_SYNTH                    ${ARG_IS_SYNTH}
        NEEDS_MIDI_INPUT            ${ARG_NEEDS_MIDI_INPUT}
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE                ${ARG_AU_MAIN_TYPE}
        VST3_CATEGORIES             ${ARG_VST3_CATEGORIES}
        VST3_AUTO_MANIFEST          FALSE
        COPY_PLUGIN_AFTER_BUILD     ${MOSAIC_COPY_PLUGIN}
        HARDENED_RUNTIME_ENABLED    TRUE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT  "Mosaic needs audio input for its Standalone app."
    )

    target_sources(${target} PRIVATE ${MOSAIC_PLUGIN_SOURCES})
    target_include_directories(${target} PRIVATE source)
    target_compile_definitions(${target} PUBLIC ${MOSAIC_JUCE_DEFINES})
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow)

    target_link_libraries(${target}
        PRIVATE
            mosaic_sources   # brings mosaic_engine with it
            mosaic_analysis
            mosaic_hub
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
endfunction()

# The plugin. Category is "effect" until the M1 results say otherwise.
mosaic_add_plugin(Mosaic
    PRODUCT_NAME     "Mosaic"
    PLUGIN_CODE      Mosc
    BUNDLE_ID        "com.richardsonmediasolutions.mosaic"
    IS_SYNTH         FALSE
    NEEDS_MIDI_INPUT FALSE
    AU_MAIN_TYPE     kAudioUnitType_Effect
    VST3_CATEGORIES  Fx
)

# M1 only: the same code as an instrument, to test whether hosts keep calling
# processBlock on empty tracks (BUILD_PLAN M1 task 6). Separate identity so both can
# coexist. Note: a separate binary means a separate Send hub — Sends in this variant
# only reach Mosaic instances of this variant.
if(MOSAIC_BUILD_INST_PROBE)
    mosaic_add_plugin(MosaicInstProbe
        PRODUCT_NAME     "Mosaic Inst Probe"
        PLUGIN_CODE      MosI
        BUNDLE_ID        "com.richardsonmediasolutions.mosaicinstprobe"
        IS_SYNTH         TRUE
        NEEDS_MIDI_INPUT TRUE
        AU_MAIN_TYPE     kAudioUnitType_MusicDevice
        VST3_CATEGORIES  Instrument
    )
endif()

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
add_subdirectory(tools)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MOSAIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(libs/Catch2)
    add_subdirectory(tests)
endif()
