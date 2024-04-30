cmake_minimum_required (VERSION 3.12)
project(VNote
    VERSION 3.17.0
    DESCRIPTION "A pleasant note-taking platform"
    HOMEPAGE_URL "https://app.vnote.fun"
    LANGUAGES C CXX)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type, defaults to Release")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(QHOTKEY_INSTALL OFF CACHE BOOL "Disable installing QHotKey" FORCE)
add_subdirectory(libs)

add_subdirectory(src)

# TODO: find a better way to organize tests
# add_subdirectory(tests)
