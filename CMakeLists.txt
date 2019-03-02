cmake_minimum_required (VERSION 3.12)
project(VNote
        VERSION 2.2.0
        DESCRIPTION "VNote is a markdown note taking application"
        HOMEPAGE_URL "https://tamlok.github.io/vnote"
        LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## Qt5 configurations
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

## Default Qt5 path
if(WIN32)
  set(Qt5_DIR "c:/Qt/Qt5.9.7/5.9.7/msvc2017_64/lib/cmake/Qt5/" CACHE PATH "directory where Qt5Config.cmake exists.")
elseif(APPLE)
  set(Qt5_DIR "/usr/local/Cellar/qt/5.9.7/clang_64/lib/cmake/Qt5/" CACHE PATH "directory where Qt5Config.cmake exists.")
else()
  set(Qt5_DIR "" CACHE PATH "directory where Qt5Config.cmake exists.")
endif()

find_package(Qt5 COMPONENTS Core Gui Network PrintSupport WebChannel WebEngine
             WebEngineWidgets Positioning Svg Widgets LinguistTools
             REQUIRED NO_DEFAULT_PATH)
## hoedown library
add_library(hoedown STATIC
            hoedown/src/autolink.c hoedown/src/document.c  hoedown/src/html.c hoedown/src/html_smartypants.c
            hoedown/src/version.c hoedown/src/buffer.c hoedown/src/escape.c hoedown/src/html_blocks.c
            hoedown/src/stack.c )
target_link_libraries(hoedown PRIVATE Qt5::Core Qt5::Gui)
## peg-highlight library
add_library(peg-highlight STATIC peg-highlight/pmh_parser.c peg-highlight/pmh_styleparser.c)
target_link_libraries(peg-highlight PRIVATE Qt5::Core Qt5::Gui)

## project sources
add_subdirectory(src)

option(CMake_RUN_CLANG_TIDY "Run clang-tidy with the compiler." OFF)
if(CMake_RUN_CLANG_TIDY)
  if(CMake_SOURCE_DIR STREQUAL CMake_BINARY_DIR)
    message(FATAL_ERROR "CMake_RUN_CLANG_TIDY requires an out-of-source build!")
  endif()
  find_program(CLANG_TIDY_COMMAND NAMES clang-tidy)
  if(NOT CLANG_TIDY_COMMAND)
    message(WARNING "CMake_RUN_CLANG_TIDY is ON but clang-tidy is not found!")
    set(CMAKE_CXX_CLANG_TIDY "" CACHE STRING "" FORCE)
  else()
    set(CLANG_TIDY_CHECKS "-*,modernize-*")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND};-checks=${CLANG_TIDY_CHECKS};-header-filter='${CMAKE_SOURCE_DIR}/src/*'")
  endif()

  # Create a preprocessor definition that depends on .clang-tidy content so
  # the compile command will change when .clang-tidy changes.  This ensures
  # that a subsequent build re-runs clang-tidy on all sources even if they
  # do not otherwise need to be recompiled.  Nothing actually uses this
  # definition.  We add it to targets on which we run clang-tidy just to
  # get the build dependency on the .clang-tidy file.
  file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy clang_tidy_sha1)
  set(CLANG_TIDY_DEFINITIONS "CLANG_TIDY_SHA1=${clang_tidy_sha1}")
  unset(clang_tidy_sha1)

endif()

option(CMake_RUN_IWYU "Run include-what-you-use with the compiler." OFF)
if(CMake_RUN_IWYU)
  find_program(IWYU_COMMAND NAMES include-what-you-use iwyu)
  if(NOT IWYU_COMMAND)
    message(WARNING "CMake_RUN_IWYU is ON but include-what-you-use is not found!")
  else()
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE
      "${IWYU_COMMAND};-Xiwyu;--mapping_file=${CMake_SOURCE_DIR}/Utilities/IWYU/mapping.imp;-w")
    list(APPEND CMAKE_CXX_INCLUDE_WHAT_YOU_USE ${CMake_IWYU_OPTIONS})
  endif()
endif()

# Clazy is a Qt oriented code checker based on clang framework. Krazy's little brother.
set(CMake_RUN_CLAZY OFF CACHE BOOL "Add clazy check for builds")
if(ENABLE_CLAZY)
  find_program(CLAZY_EXECUTABLE NAMES clazy PATHS /usr/local/llvm/bin /usr/local/bin /opt/clazy/bin)
  if(CLAZY_EXECUTABLE)
    message(STATUS "clazy found: ${CLAZY_EXECUTABLE}")
  else()
    message(AUTHOR_WARNING "clazy not found.")
  endif()
endif()


# vim: ts=2 sw=2 sts=2 et
