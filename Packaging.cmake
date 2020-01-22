find_package(Qt5Core REQUIRED)
get_target_property(_qmake_executable Qt5::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)

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
set(CPACK_IFW_ROOT $ENV{HOME}/Qt/QtIFW-3.0.6/ CACHE PATH "Qt Installer Framework installation base path")
find_program(BINARYCREATOR_EXECUTABLE binarycreator HINTS "${_qt_bin_dir}" ${CPACK_IFW_ROOT}/bin)
if(BINARYCREATOR_EXECUTABLE)
    list(APPEND CPACK_GENERATOR IFW)
    message(STATUS "   + Qt Installer Framework               YES ")
else()
    message(STATUS "   + Qt Installer Framework                NO ")
endif()

if (WIN32 AND NOT UNIX)
    #--------------------------------------------------------------------------
    # Windows specific
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}" DOC "Path to the windeployqt utility")
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
        set(CPACK_NSIS_MUI_ICON "${PACKAGE_ICON}")
        set(CPACK_NSIS_MUI_HEADERIMAGE_BITMAP "${PACKAGE_ICON}")
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

    # Bundle Library Files
    if(CMAKE_BUILD_TYPE_UPPER STREQUAL "DEBUG")
        set(WINDEPLOYQT_ARGS --debug)
    else()
        set(WINDEPLOYQT_ARGS --release)
    endif()
    file(MAKE_DIRECTORY "${CPACK_PACKAGE_DIRECTORY}/_qtstaff")
    add_custom_command(TARGET bundle PRE_BUILD
                       COMMAND "${CMAKE_COMMAND}" -E
                       env PATH="${_qt_bin_dir}" "${WINDEPLOYQT_EXECUTABLE}"
                       ${WINDEPLOYQT_ARGS}
                       --verbose 0
                       --no-compiler-runtime
                       --no-angle
                       --no-opengl-sw
                       --dir "${CPACK_PACKAGE_DIRECTORY}/_qtstaff"
                       $<TARGET_FILE:VNote>
                       COMMENT "Deploying Qt..."
                       )
    install(DIRECTORY "${CPACK_PACKAGE_DIRECTORY}/_qtstaff/" DESTINATION bin)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)

elseif (APPLE)
    #--------------------------------------------------------------------------
    # Apple specific
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}" DOC "Path to the macdeployqt utility")
    find_program(MACDEPLOYQTFIX_EXECUTABLE macdeployqtfix.py HINTS "${_qt_bin_dir}" DOC "Path to the macdeployqtfix utility")
    find_package(Python2 REQUIRED COMPONENTS Interpreter)
    message(STATUS "Package generation - Mac OS X")
    set(CPACK_PACKAGE_ICON ${CMAKE_SOURCE_DIR}/resources/Icon.icns)
    list(APPEND CPACK_GENERATOR External)
    set(CPACK_EXTERNAL_ENABLE_STAGING ON)
    set(CPACK_SET_DESTDIR ON)
    message(STATUS "   + macdeployqt -dmg                     YES ")
    set(EXTERNAL_WORK "_CPack_Packages/Darwin/External/${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-Darwin")
    set(APP_BUNDLE "${CPACK_PACKAGE_DIRECTORY}/${EXTERNAL_WORK}/${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.app")
    string(FIND "${Qt5_DIR}" "/cmake/Qt5" _STR_LOC)
    string(SUBSTRING "${Qt5_DIR}" 0 ${_STR_LOC} _QT_LIB_ROOT)
    file(GENERATE OUTPUT  "${CMAKE_BINARY_DIR}/CPackExternal.cmake"
         CONTENT "execute_process(COMMAND \"${MACDEPLOYQT_EXECUTABLE}\" \"${APP_BUNDLE}\" -always-overwrite -verbose=2 -libpath=${_QT_LIB_ROOT})
                  execute_process(COMMAND ${Python2_EXECUTABLE} \"${MACDEPLOYQTFIX_EXECUTABLE}\" \"${APP_BUNDLE}/Contents/MacOS/VNote\" ${_QT_LIB_ROOT} -v)
                  execute_process(COMMAND \"${MACDEPLOYQT_EXECUTABLE}\" \"${APP_BUNDLE}/Contents/Frameworks/QtWebEngineCore.framework/Versions/5/Helpers/QtWebEngineProcess.app\" -verbose=2 -libpath=${_QT_LIB_ROOT})
                  execute_process(COMMAND ${Python2_EXECUTABLE} \"${MACDEPLOYQTFIX_EXECUTABLE}\" \"${APP_BUNDLE}/Contents/Frameworks/QtWebEngineCore.framework/Versions/5/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess\" ${_QT_LIB_ROOT} -v)
                  execute_process(COMMAND \"${MACDEPLOYQT_EXECUTABLE}\" \"${APP_BUNDLE}\" -dmg -verbose=2 -always-overwrite -libpath=${_QT_LIB_ROOT})"
         )
    set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
    include(InstallRequiredSystemLibraries)

else ()
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

    find_program(DEBUILD_PATH debuild)
    if(DEBUILD_PATH)
        list(APPEND CPACK_GENERATOR DEB)
        message(STATUS "   + DEB                                  YES ")
        # use default, that is an output of `dpkg --print-architecture`
        #set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
        set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
        set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://tamlok.github.io/vnote")
        if(Qt5_DIR STREQUAL "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/Qt5" )
            set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        else()
            set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
            set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt5core5a, libqt5gui5, libqt5positioning5, libqt5webenginewidgets5")
        endif()
        set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "graphviz")
        set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libjs-mathjax")
        set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    endif()

    find_program(LINUXDEPLOY_EXECUTABLE linuxdeploy linuxdeploy-x86_64.AppImage HINTS "${_qt_bin_dir}")
    if(LINUXDEPLOY_EXECUTABLE)
        message(STATUS "   + AppImage                             YES ")
        find_path(NSS3_PLUGIN_PATH NAMES libsoftokn3.so PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib /usr/local/lib
                  PATH_SUFFIXES nss NO_DEFAULT_PATH)
        set(CPACK_GENERATOR "External;${CPACK_GENERATOR}")
        # run make DESTDIR=<CPACK_INSTALL_PREFIX> install before run package script
        set(CPACK_EXTERNAL_ENABLE_STAGING ON)
        set(CPACK_SET_DESTDIR ON)
        set(_EXTERNAL_PACKDIR ${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/Linux/External/${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-Linux)
        string(FIND "${Qt5_DIR}" "/cmake/Qt5" _STR_LOC)
        string(SUBSTRING "${Qt5_DIR}" 0 ${_STR_LOC} _QT_LIB_ROOT)
        set(_LINUXDEPLOY_ENV "QMAKE=${_qmake_executable}" "LD_LIBRARY_PATH=${_QT_LIB_ROOT}:$ENV{LD_LIBRARY_PATH}")
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/CPackExternal.cmake"
             CONTENT "execute_process(COMMAND ${CMAKE_COMMAND} -E env ${_LINUXDEPLOY_ENV} \"${LINUXDEPLOY_EXECUTABLE}\" --plugin qt --output appimage
                                            -v1
                                            --appdir ${_EXTERNAL_PACKDIR} -e  ${_EXTERNAL_PACKDIR}${CMAKE_INSTALL_PREFIX}/bin/VNote
                                            --desktop-file ${_EXTERNAL_PACKDIR}${CMAKE_INSTALL_PREFIX}/share/applications/vnote.desktop
                                            -i ${CMAKE_SOURCE_DIR}/src/resources/icons/16x16/vnote.png -i ${CMAKE_SOURCE_DIR}/src/resources/icons/32x32/vnote.png
                                            -i ${CMAKE_SOURCE_DIR}/src/resources/icons/48x48/vnote.png -i ${CMAKE_SOURCE_DIR}/src/resources/icons/64x64/vnote.png
                                            -i ${CMAKE_SOURCE_DIR}/src/resources/icons/128x128/vnote.png -i ${CMAKE_SOURCE_DIR}/src/resources/icons/256x256/vnote.png
                                            -i ${CMAKE_SOURCE_DIR}/src/resources/icons/vnote.svg
                                            -l ${NSS3_PLUGIN_PATH}/libfreebl3.so -l ${NSS3_PLUGIN_PATH}/libfreeblpriv3.so -l ${NSS3_PLUGIN_PATH}/libnssckbi.so
                                            -l ${NSS3_PLUGIN_PATH}/libnssdbm3.so -l ${NSS3_PLUGIN_PATH}/libsoftokn3.so
                                      WORKING_DIRECTORY ${CPACK_PACKAGE_DIRECTORY})"
             )
        set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
    else()
        message(STATUS "   + AppImage                              NO ")
    endif()

    # set package icon
    set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/src/resources/icons/vnote.png")
endif()

include(CPack)
