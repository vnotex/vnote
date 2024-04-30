cmake_minimum_required(VERSION 3.16)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_INSTALL_BINDIR "." CACHE STRING "Binary dir for install")

set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "Qt version to use (5 or 6), defaults to 6")
find_package(Qt${QT_DEFAULT_MAJOR_VERSION} REQUIRED COMPONENTS Core Gui Network PrintSupport Sql Svg Widgets WebChannel WebEngineWidgets LinguistTools)
find_package(Qt${QT_DEFAULT_MAJOR_VERSION} OPTIONAL_COMPONENTS Core5Compat)

if ((QT_DEFAULT_MAJOR_VERSION GREATER 5))
    qt_standard_project_setup()
else()
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

# Application icon on Windows
set(VX_APP_ICON_RC_WIN data/core/icons/vnote.rc)

# Application icon on macOS
set(MACOSX_BUNDLE_ICON_FILE vnote.icns)
set(VX_APP_ICON_MACOS data/core/icons/vnote.icns)
set_source_files_properties(${VX_APP_ICON_MACOS} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")

# Translations
set(VX_TS_FILES data/core/translations/vnote_zh_CN.ts
    data/core/translations/vnote_ja.ts)
if((QT_DEFAULT_MAJOR_VERSION GREATER 5))
    qt_add_lupdate(vnote TS_FILES ${VX_TS_FILES})
endif()
# Generate .qm files from .ts files (lrelease)
qt_add_translation(VX_QM_FILES ${VX_TS_FILES})
add_custom_target(lrelease DEPENDS ${VX_QM_FILES})

# TODO: VTextEdit translations
list(APPEND VX_QM_FILES
    data/core/translations/qdialogbuttonbox_zh_CN.qm
    data/core/translations/qtbase_ja.qm
    data/core/translations/qtbase_zh_CN.qm
    data/core/translations/qtv_ja.qm
    data/core/translations/qtv_zh_CN.qm
    data/core/translations/qwebengine_zh_CN.qm
)
install(FILES ${VX_QM_FILES} DESTINATION "${CMAKE_INSTALL_BINDIR}/translations" OPTIONAL)

# Resources
set(VX_RESOURCE_FILES data/core/core.qrc)
qt_add_binary_resources(VX_EXTRA_RESOURCE data/extra/extra.qrc DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/vnote_extra.rcc
    OPTIONS -compress 9)

add_executable(vnote WIN32 MACOSX_BUNDLE
    application.cpp application.h
    commandlineoptions.cpp commandlineoptions.h
    fakeaccessible.cpp fakeaccessible.h
    main.cpp
    ${VX_APP_ICON_RC_WIN} ${VX_APP_ICON_MACOS} ${VX_RESOURCE_FILES}
)

add_dependencies(vnote VX_EXTRA_RESOURCE)

set(VX_LIBS_FOLDER ../libs)
target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_program(GOLD_LINKER "ld.gold")
if (NOT ${GOLD_LINKER} STREQUAL GOLD_LINKER-NOTFOUND)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
endif()

target_compile_definitions(vnote PRIVATE
    QT_MESSAGELOGCONTEXT
)

add_subdirectory(core)
add_subdirectory(export)
add_subdirectory(imagehost)
add_subdirectory(search)
add_subdirectory(snippet)
add_subdirectory(task)
add_subdirectory(unitedentry)
add_subdirectory(utils)
add_subdirectory(widgets)

target_link_libraries(vnote PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::PrintSupport
    Qt::Sql
    Qt::Svg
    Qt::WebChannel
    Qt::WebEngineWidgets
    Qt::Widgets
    VTextEdit
    qhotkey
)

if((QT_DEFAULT_MAJOR_VERSION GREATER 5))
    target_link_libraries(vnote PRIVATE
        Qt::Core5Compat
    )
endif()

# Copy the qt.conf on Windows
if(MSVC)
    add_custom_command(TARGET vnote POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/package/qt.conf" $<TARGET_FILE_DIR:vnote>)
endif()

# Installation
if (MSVC)
    install(TARGETS vnote
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${PROJECT_SOURCE_DIR}/package/qt.conf"
        DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

include(${CMAKE_CURRENT_LIST_DIR}/Packaging.cmake)
