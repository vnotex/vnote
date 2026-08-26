# GUI Utilities
# Contains GUI-dependent utility functions (QPixmap, QColor, QPalette, etc.)

target_sources(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/commentcolorswatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/commentcolorswatch.h
    ${CMAKE_CURRENT_SOURCE_DIR}/guiutils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/guiutils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/iconutils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/iconutils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imageutils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imageutils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/treehorizontalscrollhelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/treehorizontalscrollhelper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/themeutils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/themeutils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/textcursorpreserver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/textcursorpreserver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/widgetutils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/widgetutils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/printutils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/printutils.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
