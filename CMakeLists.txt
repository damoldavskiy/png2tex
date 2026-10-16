cmake_minimum_required(VERSION 3.20)
project(png2tex LANGUAGES CXX)

# Point these at the downloaded NVTT 3 SDK. The library name varies by version.
set(NVTT_INCLUDE_DIR "" CACHE PATH "Directory containing nvtt/nvtt.h")
set(NVTT_LIBRARY "" CACHE FILEPATH "NVTT 3 import library (.lib) or shared library (.so)")
if(NOT EXISTS "${NVTT_INCLUDE_DIR}/nvtt/nvtt.h" OR NOT EXISTS "${NVTT_LIBRARY}")
    message(FATAL_ERROR "Set NVTT_INCLUDE_DIR and NVTT_LIBRARY to your NVTT 3 SDK paths.")
endif()

add_executable(png2tex main.cpp)
target_compile_features(png2tex PRIVATE cxx_std_17)
target_include_directories(png2tex PRIVATE "${NVTT_INCLUDE_DIR}")
target_link_libraries(png2tex PRIVATE "${NVTT_LIBRARY}")
