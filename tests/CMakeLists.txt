list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/libs/Catch2/extras")
include(Catch)

# ---------------------------------------------------------------------------
# Plain C++ tests: engine + hub. No JUCE (CLAUDE.md, "Engine boundaries").
# ---------------------------------------------------------------------------
add_executable(mosaic_tests
    test_engine_info.cpp
    test_send_hub.cpp
    test_grid_clock.cpp
    test_step_chooser.cpp
    test_slice_position.cpp
    test_tile_renderer.cpp
    test_file_slot_source.cpp
    test_key_snap.cpp
    test_key_detector.cpp
    test_pitch_shifter.cpp
)
target_link_libraries(mosaic_tests PRIVATE
    mosaic_sources   # brings mosaic_engine with it
    mosaic_analysis
    signalsmith-stretch   # the shifter experiment drives the library directly
    mosaic_hub
    Catch2::Catch2WithMain
)
target_compile_options(mosaic_tests PRIVATE -Wall -Wextra)
catch_discover_tests(mosaic_tests)

# ---------------------------------------------------------------------------
# Plugin tests: the real processor driven in-process with a fake playhead.
# Compiled as a console app so no host is needed (routing null test, bus test).
# ---------------------------------------------------------------------------
juce_add_console_app(mosaic_plugin_tests PRODUCT_NAME "mosaic_plugin_tests")

target_sources(mosaic_plugin_tests PRIVATE
    test_plugin_routing.cpp
    test_plugin_mosaic.cpp
    test_plugin_key.cpp
    ${MOSAIC_PLUGIN_SOURCES_ABS}
)
target_include_directories(mosaic_plugin_tests PRIVATE ${CMAKE_SOURCE_DIR}/source)
set(MOSAIC_TEST_JUCE_DEFINES ${MOSAIC_JUCE_DEFINES})
list(REMOVE_ITEM MOSAIC_TEST_JUCE_DEFINES JUCE_MODAL_LOOPS_PERMITTED=0)
target_compile_definitions(mosaic_plugin_tests PRIVATE
    ${MOSAIC_TEST_JUCE_DEFINES}
    JUCE_MODAL_LOOPS_PERMITTED=1      # tests pump the message queue with runDispatchLoopUntil
    JUCE_STANDALONE_APPLICATION=1
    JucePlugin_Name="Mosaic Test"
    JucePlugin_WantsMidiInput=0
    JucePlugin_IsSynth=0
)
target_link_libraries(mosaic_plugin_tests PRIVATE
    mosaic_sources
    mosaic_analysis
    mosaic_hub
    Catch2::Catch2WithMain
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)
# Not juce_recommended_warning_flags: it adds -Wfloat-equal, and exact compares are the point here.
target_compile_options(mosaic_plugin_tests PRIVATE -Wall -Wextra -Wshadow)
catch_discover_tests(mosaic_plugin_tests)
