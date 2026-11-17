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
)
target_link_libraries(mosaic_tests PRIVATE
    mosaic_engine
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
    ${CMAKE_SOURCE_DIR}/source/plugin/Parameters.cpp
    ${CMAKE_SOURCE_DIR}/source/plugin/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/source/plugin/PluginEditor.cpp
)
target_include_directories(mosaic_plugin_tests PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_definitions(mosaic_plugin_tests PRIVATE
    ${MOSAIC_JUCE_DEFINES}
    JUCE_STANDALONE_APPLICATION=1
    JucePlugin_Name="Mosaic Test"
    JucePlugin_WantsMidiInput=0
    JucePlugin_IsSynth=0
)
target_link_libraries(mosaic_plugin_tests PRIVATE
    mosaic_engine
    mosaic_hub
    Catch2::Catch2WithMain
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)
# Not juce_recommended_warning_flags: it adds -Wfloat-equal, and exact compares are the point here.
target_compile_options(mosaic_plugin_tests PRIVATE -Wall -Wextra -Wshadow)
catch_discover_tests(mosaic_plugin_tests)
