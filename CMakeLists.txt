cmake_minimum_required(VERSION 3.25)

project(onekey
    VERSION 0.1.0
    DESCRIPTION "One-Key Input — Windows AI dictation tool (C++ Core)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /Zc:__cplusplus /EHsc)
    add_compile_definitions(
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
    )
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(SherpaOnnx)

enable_testing()

add_subdirectory(core)
