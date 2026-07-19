cmake_minimum_required(VERSION 3.22.1)
project("mediaengine")

# Anchor the search to the exact source directory to prevent CI build folder crashes
file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(mediaengine SHARED ${SRC_FILES})

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(mediaengine ${log-lib} ${android-lib})