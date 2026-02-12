# UI layer for VNote clean architecture
# Contains MainWindow2 and future UI components

qt_add_library(ui2 STATIC
    mainwindow2.cpp mainwindow2.h
)

target_include_directories(ui2 PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ui2 PUBLIC
    Qt::Core
    Qt::Gui
    Qt::Widgets
)
