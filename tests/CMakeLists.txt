# Unit tests: Catch2 v3, linked against the plain-C++ engine only.
# The plugin modules are deliberately not linked here (CLAUDE.md, "Engine boundaries").

add_executable(mosaic_tests
    test_engine_info.cpp
)

target_link_libraries(mosaic_tests PRIVATE
    mosaic_engine
    Catch2::Catch2WithMain
)

target_compile_options(mosaic_tests PRIVATE -Wall -Wextra)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/libs/Catch2/extras")
include(Catch)
catch_discover_tests(mosaic_tests)
