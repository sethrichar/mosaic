# Command-line tools. Plain C++ unless stated.
add_executable(mosaic_engine_demo engine_demo/main.cpp)
target_link_libraries(mosaic_engine_demo PRIVATE mosaic_engine)
target_compile_options(mosaic_engine_demo PRIVATE -Wall -Wextra)

# keydetect: scores the key detector against a folder of known-key files (M4 task 8).
juce_add_console_app(mosaic_keydetect PRODUCT_NAME "mosaic_keydetect")
target_sources(mosaic_keydetect PRIVATE
    keydetect/main.cpp
    ${CMAKE_SOURCE_DIR}/source/sources/FileLoader.cpp
)
target_include_directories(mosaic_keydetect PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_definitions(mosaic_keydetect PRIVATE ${MOSAIC_JUCE_DEFINES} JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(mosaic_keydetect PRIVATE
    mosaic_sources
    mosaic_analysis
    juce::juce_audio_formats
    juce::juce_events
    juce::juce_recommended_config_flags
)
