cmake_minimum_required(VERSION 3.22.1)
project("mediaengine" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mediaengine SHARED
    media_engine.cpp
    audio_processor.cpp
    loudness_enhancer.cpp
    offline_buffer.cpp
    jni_bridge.cpp
)

find_library(log-lib log)
find_library(android-lib android)
find_library(OpenSLES-lib OpenSLES)

target_include_directories(mediaengine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mediaengine
    ${log-lib}
    ${android-lib}
    ${OpenSLES-lib}
)

target_compile_options(mediaengine PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -DMEDIA_ENGINE_EXPORTS
)