add_executable(VNote MACOSX_BUNDLE main.cpp)

file(GLOB SRC_FILES *.cpp)
file(GLOB DIALOG_SRCS dialog/*.cpp)
file(GLOB UTILS_SRCS utils/*.cpp)
file(GLOB WIDGETS_SRCS widgets/*.cpp)
file(GLOB QRC_FILES *.qrc)
file(GLOB TRANSLATIONS translations/*.qm)

target_sources(VNote PRIVATE ${SRC_FILES})
target_sources(VNote PRIVATE ${DIALOG_SRCS})
target_sources(VNote PRIVATE ${UTILS_SRCS})
target_sources(VNote PRIVATE ${WIDGETS_SRCS})
target_sources(VNote PRIVATE ${QRC_FILES})

include_directories(dialog utils widgets)

# Qt5 libraries
target_link_libraries(VNote PRIVATE Qt5::Core Qt5::WebEngine Qt5::WebEngineWidgets
                      Qt5::Network Qt5::PrintSupport Qt5::WebChannel Qt5::Widgets
                      Qt5::PrintSupport Qt5::Svg)
set_property(TARGET VNote PROPERTY AUTORCC_OPTIONS "--compress;9")

# Thirdparty libraries
target_include_directories(VNote PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                         ${CMAKE_SOURCE_DIR}/peg-highlight ${CMAKE_SOURCE_DIR}/hoedown)
target_link_libraries(VNote PRIVATE peg-highlight hoedown)

# Compile options
if(GCC_VERSION VERSION_GREATER_EQUAL 8.0)
  target_compile_options(VNote PRIVATE "-Wno-class-memaccess")
endif()

if (WIN32 AND NOT UNIX)
    # MSVC and not mingw32
    install(TARGETS VNote RUNTIME DESTINATION bin)
    install(FILES ${TRANSLATIONS} DESTINATION translations )
elseif (APPLE)
    set_target_properties(VNote PROPERTIES MACOSX_PACKAGE_LOCATION "${PROJECT_NAME}.app/Contents")
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "/Applications" CACHE PATH "Reset installation path to MacOS default." FORCE)
    endif()
    install(TARGETS VNote BUNDLE DESTINATION . COMPONENT Runtime
            RUNTIME DESTINATION bin COMPONENT Runtime)
    install(FILES ${TRANSLATIONS} DESTINATION translations COMPONENT Runtime)
    set(MACOSX_BUNDLE_BUNDLE_NAME "VNote")
    set(MACOSX_BUNDLE_BUNDLE_GUI_IDENTIFIER "com.tamlok.VNote")
    set(MACOSX_BUNDLE_ICON_FILE ${CMAKE_SOURCE_DIR}/src/resources/icons/vnote.icns)
    set(MACOSX_BUNDLE_BUNDLE_VERSION "VNOTE ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
    set(MACOSX_BUNDLE_LONG_VERSION_STRING ${MACOSX_BUNDLE_BUNDLE_VERSION})
    # Set short version independent with project version to be able to increment independendently.
    math(EXPR SHORT_VERSION_MAJOR "${PROJECT_VERSION_MAJOR} * 100 + ${PROJECT_VERSION_MINOR}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${SHORT_VERSION_MAJOR}.${PROJECT_VERSION_PATCH}.0")
    set(MACOSX_BUNDLE_EXECUTABLE_NAME "VNote")
    set(MACOSX_BUNDLE_COPYRIGHT "Distributed under MIT license. Copyright 2016-2019 Le Tan")
    set(MACOSX_BUNDLE_INFO_STRING "VNote is a note-taking application that knows programmers and Markdown better. Distributed under MIT license. Copyright 2017 Le Tan")

    set_source_files_properties(${MACOSX_BUNDLE_ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
else()
    # Linux, mingw32
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Reset installation path to MacOS default." FORCE)
    endif()
    install(TARGETS VNote RUNTIME DESTINATION bin)
    install(FILES ${TRANSLATIONS} DESTINATION translations )

    set(desktop.path applications)
    set(desktop.files vnote.desktop)

    set(icon16.path icons/hicolor/16x16/apps)
    set(icon16.files resources/icons/16x16/vnote.png)

    set(icon32.path icons/hicolor/32x32/apps)
    set(icon32.files resources/icons/32x32/vnote.png)

    set(icon48.path icons/hicolor/48x48/apps)
    set(icon48.files resources/icons/48x48/vnote.png)

    set(icon64.path icons/hicolor/64x64/apps)
    set(icon64.files resources/icons/64x64/vnote.png)

    set(icon128.path icons/hicolor/128x128/apps)
    set(icon128.files resources/icons/128x128/vnote.png)

    set(icon256.path icons/hicolor/256x256/apps)
    set(icon256.files resources/icons/256x256/vnote.png)

    set(iconsvg.path icons/hicolor/scalable/apps)
    set(iconsvg.files resources/icons/vnote.svg)

    foreach(items IN ITEMS desktop icon16 icon32 icon48 icon64 icon128 icon256 iconsvg)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${${items}.files}
                DESTINATION share/${${items}.path}
                PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    endforeach()
    install(FILES ${CMAKE_SOURCE_DIR}/LICENSE
        DESTINATION share/doc/vnote/
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        RENAME copyright)
endif()

