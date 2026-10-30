# CMakeLists.txt
# For building maet_jni as Android JNI shared library

cmake_minimum_required(VERSION 3.22.1)

project(maet-jni)

# Enable C++17 for <vector>, <string>, etc.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define the shared library
add_library(
    maet_jni           # Library name → libmaet_jni.so
    SHARED               # Shared library (required for JNI)
    maet_jni.cpp         # Use .cpp for C++ support
)

# Find and link Android libraries
find_library(log-lib log)
find_library(android-lib android)

# Include directories
target_include_directories(maet_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries
target_link_libraries(maet_jni PRIVATE
    ${log-lib}           # liblog.so
    ${android-lib}       # libandroid.so (Required for ANativeWindow_fromSurface and other ANativeWindow_* functions)
    jnigraphics          # Required for AndroidBitmap_* functions
)
