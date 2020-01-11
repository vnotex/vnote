
find_package(Qt5Core REQUIRED)
get_target_property(_qmake_executable Qt5::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
find_program(LINUXDEPLOYQT_EXECUTABLE linuxdeployqt linuxdeployqt-continuous-x86_64.AppImage HINTS "${_qt_bin_dir}")
find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}")
find_program(MACDEPLOYQTFIX_EXECUTABLE macdeployqtfix.py HINTS "${_qt_bin_dir}")
find_package(Python)

set(CPACK_IFW_ROOT $ENV{HOME}/Qt/QtIFW-3.0.6/ CACHE PATH "Qt Installer Framework installation base path")
find_program(BINARYCREATOR_EXECUTABLE binarycreator HINTS "${_qt_bin_dir}" ${CPACK_IFW_ROOT}/bin)

mark_as_advanced(WINDEPLOYQT_EXECUTABLE LINUXDEPLOYQT_EXECUTABLE)
mark_as_advanced(MACDEPLOYQT_EXECUTABLE MACDEPLOYQTFIX_EXECUTABLE)

function(linuxdeployqt destdir desktopfile)
    # creating AppDir
    add_custom_command(TARGET bundle PRE_BUILD
                       COMMAND "${CMAKE_MAKE_PROGRAM}" DESTDIR=${destdir} install
                       COMMAND "${LINUXDEPLOYQT_EXECUTABLE}" ${destdir}/${CMAKE_INSTALL_PREFIX}/${desktopfile} -bundle-non-qt-libs
                               -qmake=${_qmake_executable}
                       # hot fix for a known issue for libnss3 and libnssutils3.
                       COMMAND ${CMAKE_COMMAND} -E copy_directory ${NSS3_PLUGIN_PATH}
                                                                  ${destdir}/${CMAKE_INSTALL_PREFIX}/lib/
                       WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    # packaging AppImage
    add_custom_command(TARGET bundle POST_BUILD
                       COMMAND "${LINUXDEPLOYQT_EXECUTABLE}"  ${destdir}/${CMAKE_INSTALL_PREFIX}/${desktopfile}
                               -appimage -qmake=${_qmake_executable}
                       WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/packaging)
endfunction()

function(windeployqt target)

    # Bundle Library Files
    if(CMAKE_BUILD_TYPE_UPPER STREQUAL "DEBUG")
        set(WINDEPLOYQT_ARGS --debug)
    else()
        set(WINDEPLOYQT_ARGS --release)
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
                       COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/winqt/"
                       COMMAND "${CMAKE_COMMAND}" -E
                               env PATH="${_qt_bin_dir}" "${WINDEPLOYQT_EXECUTABLE}"
                               ${WINDEPLOYQT_ARGS}
                               --verbose 0
                               --no-compiler-runtime
                               --no-angle
                               --no-opengl-sw
                               --dir "${CMAKE_CURRENT_BINARY_DIR}/winqt/"
                               $<TARGET_FILE:${target}>
                       COMMENT "Deploying Qt..."
    )
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/winqt/" DESTINATION bin)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
endfunction()

function(macdeployqt bundle targetdir _PACKAGER)
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/CPackMacDeployQt-${_PACKAGER}.cmake
         CONTENT "set(APP_BUNDLE_DIR \"${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/Darwin/${_PACKAGER}/${targetdir}/${bundle}\")
                  execute_process(
                    COMMAND \"${MACDEPLOYQT_EXECUTABLE}\" \"${APP_BUNDLE_DIR}}\" -always-overwrite
                    COMMAND ${Python_EXECUTABLE} \"${MACDEPLOYQTFIX_EXECUTABLE}\" \"${APP_BUNDLE_DIR}/Contents/MacOS/VNote\" ${qt_bin_dir}/../
                    COMMAND \"${MACDEPLOYQT_EXECUTABLE}\" \"${APP_BUNDLE_DIR}>/Contents/Frameworks/QtWebEngineCore.framework/Versions/5/Helpers/QtWebEngineProcess.app
                    COMMAND ${Python_EXECUTABLE} \"${MACDEPLOYQTFIX_EXECUTABLE}\" \"${APP_BUNDLE_DIR}>/Contents/Frameworks/QtWebEngineCore.framework/Versions/5/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess\" ${qt_bin_dir}/../
                  )"
    )

    install(SCRIPT ${CMAKE_BINARY_DIR}/CPackMacDeployQt-${_PACKAGER}.cmake COMPONENT Runtime)
    include(InstallRequiredSystemLibraries)
endfunction()

set(CPACK_PACKAGE_VENDOR "Le Tan")
set(CPACK_PACKAGE_NAME "vnote")
set(CPACK_PACKAGE_CONTACT "Le Tan <tamlokveer@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

set(CPACK_PACKAGE_INSTALL_DIRECTORY "vnote")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")

# set human names to executables
set(CPACK_PACKAGE_EXECUTABLES "VNote" "VNote")
set(CPACK_CREATE_DESKTOP_LINKS "VNote")
set(CPACK_STRIP_FILES TRUE)

#------------------------------------------------------------------------------
# include CPack, so we get target for packages
set(CPACK_OUTPUT_CONFIG_FILE "${CMAKE_BINARY_DIR}/BundleConfig.cmake")

add_custom_target(bundle
                  COMMAND ${CMAKE_CPACK_COMMAND} "--config" "${CMAKE_BINARY_DIR}/BundleConfig.cmake"
                  COMMENT "Running CPACK. Please wait..."
                  DEPENDS VNote)
set(CPACK_GENERATOR)

# Qt IFW packaging framework
if(BINARYCREATOR_EXECUTABLE)
    list(APPEND CPACK_GENERATOR IFW)
    message(STATUS "   + Qt Installer Framework               YES ")
else()
    message(STATUS "   + Qt Installer Framework                NO ")
endif()

if(WIN32 AND NOT UNIX)
    #--------------------------------------------------------------------------
    # Windows specific
    list(APPEND CPACK_GENERATOR ZIP)
    message(STATUS "Package generation - Windows")
    message(STATUS "   + ZIP                                  YES ")

    set(PACKAGE_ICON "${CMAKE_SOURCE_DIR}/src/resources/icons/vnote.ico")

    # NSIS windows installer
    find_program(NSIS_PATH nsis PATH_SUFFIXES nsis)
    if(NSIS_PATH)
        list(APPEND CPACK_GENERATOR NSIS)
        message(STATUS "   + NSIS                                 YES ")

        set(CPACK_NSIS_DISPLAY_NAME ${CPACK_PACKAGE_NAME})
        # Icon of the installer
        file(TO_NATIVE_PATH "${PACKAGE_ICON}" CPACK_NSIS_MUI_ICON)
        file(TO_NATIVE_PATH "${PACKAGE_ICON}" CPACK_NSIS_MUI_HEADERIMAGE_BITMAP)
        set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
        set(CPACK_NSIS_MODIFY_PATH ON)
    else()
        message(STATUS "   + NSIS                                 NO ")
    endif()

    # NuGet package
    find_program(NUGET_EXECUTABLE nuget)
    if(NUGET_EXECUTABLE)
        list(APPEND CPACK_GENERATOR NuGET)
        message(STATUS "   + NuGET                               YES ")
        set(CPACK_NUGET_PACKAGE_NAME "VNote")
    else()
        message(STATUS "   + NuGET                                NO ")
    endif()

    windeployqt(VNote)

elseif(APPLE)
    #--------------------------------------------------------------------------
    # Apple specific
    message(STATUS "Package generation - Mac OS X")
    message(STATUS "   + TBZ2                                 YES ")

    list(APPEND CPACK_GENERATOR TBZ2)
    set(CPACK_PACKAGE_ICON ${CMAKE_SOURCE_DIR}/resources/Icon.icns)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
    macdeployqt("${PROJECT_NAME}.app" "${PROJECT_NAME}-${PROJECT_VERSION}-Darwin" "TBZ")

    # XXX: not working settings for bundle and dragndrop generator
    set(CPACK_BUNDLE_NAME "${PROJECT_NAME}" )
    set(CPACK_BUNDLE_PLIST "${CMAKE_BINARY_DIR}/Info.plist")
    set(CPACK_BUNDLE_ICON ${CMAKE_PACKAGE_ICON})
    set(CPACK_DMG_VOLUME_NAME "${PROJECT_NAME}")
    set(CPACK_DMG_FORMAT "UDBZ")
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_SOURCE_DIR}/src/resources/icons/vnote.png")

    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.13)
        list(APPEND CPACK_GENERATOR External)
        message(STATUS "   + macdeployqt -dmg                     YES ")
        configure_file(${CMAKE_SOURCE_DIR}/CPackMacDeployQt.cmake.in "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
        set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
    endif()

else()
    #-----------------------------------------------------------------------------
    # Linux specific
    list(APPEND CPACK_GENERATOR TBZ2 TXZ)
    message(STATUS "Package generation - UNIX")
    message(STATUS "   + TBZ2                                 YES ")
    message(STATUS "   + TXZ                                  YES ")

    find_program(RPMBUILD_PATH rpmbuild)
    if(RPMBUILD_PATH)
        message(STATUS "   + RPM                                  YES ")
        set(CPACK_GENERATOR "${CPACK_GENERATOR};RPM")
        set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    else()
        message(STATUS "   + RPM                                  NO ")
    endif()

    list(APPEND CPACK_GENERATOR DEB)
    message(STATUS "   + DEB                                  YES ")
    # use default, that is an output of `dpkg --print-architecture`
    #set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://tamlok.github.io/vnote")
    # check if dependencies exist in standard path, i.e standard package
    # ubuntu bionic or later has it.
    find_path(QT5WEBENGINEWIDGET_PATH
              NAMES libQt5WebEngineWidgets.so
              PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib
              NO_DEFAULT_PATH)
    if(QT5WEBENGINEWIDGET_PATH)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    else()
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt5core5a, libqt5gui5, libqt5positioning5, libqt5webenginewidgets5")
    endif()
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "graphviz")
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libjs-mathjax")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")

    if(LINUXDEPLOYQT_EXECUTABLE)
        message(STATUS "   + AppImage                             YES ")
        find_path(NSS3_PLUGIN_PATH NAMES libsoftokn3.so PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib /usr/local/lib
                  PATH_SUFFIXES nss NO_DEFAULT_PATH)
        if(CMAKE_VERSION VERSION_LESS 3.13)
            linuxdeployqt("${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/Linux/AppImage" "share/applications/vnote.desktop")
        else()
            set(CPACK_GENERATOR "External;${CPACK_GENERATOR}")
            configure_file(${CMAKE_SOURCE_DIR}/CPackLinuxDeployQt.cmake.in "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
            set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
        endif()
    else()
        message(STATUS "   + AppImage                              NO ")
    endif()

    # set package icon
    set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/src/resources/icons/vnote.png")
endif()

include(CPack)
