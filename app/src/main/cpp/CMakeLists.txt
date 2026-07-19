cmake_minimum_required(VERSION 3.22.1)
project("mediaengine")

# Automatically grab every .cpp file in this directory and its subdirectories
file(GLOB_RECURSE SRC_FILES "*.cpp")

# Build the shared hardware library using all found files
add_library(mediaengine SHARED ${SRC_FILES})

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(mediaengine ${log-lib} ${android-lib})