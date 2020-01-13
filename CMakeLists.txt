cmake_minimum_required (VERSION 3.13)
project(VNote VERSION 2.8.2
        DESCRIPTION "VNote is a markdown note taking application"
        HOMEPAGE_URL "https://tamlok.github.io/vnote"
        LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## Qt5 configurations
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# find Qt5 installation
find_package(Qt5 REQUIRED COMPONENTS Core Gui Network PrintSupport WebChannel WebEngine
             WebEngineWidgets Positioning Svg Widgets LinguistTools
             HINTS
             "/opt/qt59/lib/cmake/Qt5/"
             "c:/Qt/Qt5.9.8/5.9.8/msvc2017_64/lib/cmake/Qt5/"
             "c:/Qt/Qt5.9.9/5.9.9/msvc2017_64/lib/cmake/Qt5/"
             "/usr/local/Cellar/qt/5.9.8/clang_64/lib/cmake/Qt5/"
             "/usr/local/Cellar/qt/5.9.9/clang_64/lib/cmake/Qt5/")

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

include(${CMAKE_CURRENT_LIST_DIR}/Packaging.cmake)

# vim: ts=2 sw=2 sts=2 et
