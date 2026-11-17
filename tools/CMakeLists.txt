# Command-line tools. Plain C++ unless stated.
add_executable(mosaic_engine_demo engine_demo/main.cpp)
target_link_libraries(mosaic_engine_demo PRIVATE mosaic_engine)
target_compile_options(mosaic_engine_demo PRIVATE -Wall -Wextra)
