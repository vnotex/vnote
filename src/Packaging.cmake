# from: https://github.com/miurahr/cmake-qt-packaging-example
find_package(Qt${QT_DEFAULT_MAJOR_VERSION} REQUIRED COMPONENTS Core)

get_target_property(QMAKE_EXECUTABLE Qt::qmake IMPORTED_LOCATION)
get_filename_component(QT_BIN_DIR "${QMAKE_EXECUTABLE}" DIRECTORY)
execute_process(COMMAND ${QMAKE_EXECUTABLE} -query QT_VERSION OUTPUT_VARIABLE QT_VERSION)

set(QT_TOOLS_DIR "${QT_BIN_DIR}/../../../Tools")
cmake_path(NORMAL_PATH QT_TOOLS_DIR OUTPUT_VARIABLE QT_TOOLS_DIR)

set(QT_PLUGINS_DIR "${QT_BIN_DIR}/../plugins")
cmake_path(NORMAL_PATH QT_PLUGINS_DIR OUTPUT_VARIABLE QT_PLUGINS_DIR)

# To use the specific version of Qt
set(WINDEPLOYQT_EXECUTABLE "${QT_BIN_DIR}/windeployqt.exe")

find_program(LINUXDEPLOY_EXECUTABLE linuxdeploy linuxdeploy-x86_64.AppImage HINTS "${QT_BIN_DIR}")
find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${QT_BIN_DIR}")
find_program(MACDEPLOYQTFIX_EXECUTABLE macdeployqtfix.py HINTS "${QT_BIN_DIR}")
find_package(Python)

function(windeployqt target)
    # Bundle Library Files
    string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_UPPER)

    if ((QT_DEFAULT_MAJOR_VERSION GREATER 5))
        if(CMAKE_BUILD_TYPE_UPPER STREQUAL "DEBUG")
            set(WINDEPLOYQT_ARGS --debug)
        else()
            set(WINDEPLOYQT_ARGS --release)
        endif()
    endif()

    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/winqt/"
        COMMAND "${CMAKE_COMMAND}" -E
            env PATH="${QT_BIN_DIR}" "${WINDEPLOYQT_EXECUTABLE}"
            ${WINDEPLOYQT_ARGS}
            --no-quick-import
            --no-opengl-sw
            --no-compiler-runtime
            --translations zh_CN,ja
            --dir "${CMAKE_CURRENT_BINARY_DIR}/winqt/"
            $<TARGET_FILE:${target}>
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/winqt/generic/"
        # Keep winqt/styles/: it holds qwindows11style.dll, without which Qt 6
        # falls back to the legacy windowsvista style on Windows 11 (beveled
        # QFrame::StyledPanel borders instead of the modern rounded panels).
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/winqt/qmltooling/"
        COMMENT "Deploying Qt..."
        DEPENDS vnote lrelease
    )

    add_dependencies(pack deploy)

    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/winqt/" DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL)

    set(OPENSSL_ROOT_DIR "${QT_TOOLS_DIR}/OpenSSL/Win_x64" CACHE STRING "OpenSSL dir")
    file(GLOB OPENSSL_LIBS_FILES "${OPENSSL_ROOT_DIR}/bin/lib*.dll")
    cmake_path(NORMAL_PATH OPENSSL_LIBS_FILES OUTPUT_VARIABLE OPENSSL_LIBS_FILES)
    install(FILES ${OPENSSL_LIBS_FILES} DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL)

    message(STATUS "OpenSSLExtraLIBDIR:${OPENSSL_EXTRA_LIB_DIR}")
    file(GLOB OPENSSL_EXTRA_LIB_FILES "${OPENSSL_EXTRA_LIB_DIR}/lib*.dll")
    cmake_path(NORMAL_PATH OPENSSL_EXTRA_LIB_FILES OUTPUT_VARIABLE OPENSSL_EXTRA_LIB_FILES)
    message(STATUS "OpenSSLExtraLibFiles:${OPENSSL_EXTRA_LIB_FILES}")
    install(FILES ${OPENSSL_EXTRA_LIB_FILES} DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL)

    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)

    # InstallRequiredSystemLibraries defaults this to "bin" on Windows,
    # INDEPENDENTLY of where everything else is installed. Leaving it unset
    # would drop the CRT/UCRT DLLs into a "bin/" subdirectory of an otherwise
    # flat package, i.e. NOT beside vnote.exe, and the app would fail to start.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")

    # InstallRequiredSystemLibraries is CMake-version-bound: for a toolset newer
    # than the running CMake (e.g. VS 18 / MSVC v145 under CMake 3.30) it mis-maps
    # MSVC_TOOLSET_VERSION to 143, looks for a non-existent "Microsoft.VC143.CRT"
    # folder under a "MSVC_REDIST_DIR-NOTFOUND" path, and emits a noisy "system
    # runtime library file does not exist" warning for every CRT DLL while
    # bundling nothing. Silence those warnings; the module still resolves the
    # UCRT / MFC pieces and, on a toolset it DOES understand, the VC CRT.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS TRUE)
    include(InstallRequiredSystemLibraries)

    # Fallback ONLY when the module failed to resolve the VC C runtime (unknown
    # toolset): locate the real "Microsoft.VC*.CRT" redist folder from the
    # compiler path and install its DLLs ourselves. Gating on the module having
    # failed keeps it the sole CRT installer for known toolsets, so a machine
    # with several toolsets never gets two competing / mismatched install rules.
    if(MSVC AND (NOT DEFINED MSVC_CRT_DIR OR NOT EXISTS "${MSVC_CRT_DIR}"))
        # .../VC/Tools/MSVC/<ver>/bin/Host<arch>/<target-arch>/cl.exe
        get_filename_component(_vc_arch_dir "${CMAKE_CXX_COMPILER}" DIRECTORY) # .../<target-arch>
        get_filename_component(_vc_redist_arch "${_vc_arch_dir}" NAME)        # x64 / x86 / arm / arm64
        get_filename_component(_vc_root "${_vc_arch_dir}" DIRECTORY)          # .../Host<arch>
        get_filename_component(_vc_root "${_vc_root}" DIRECTORY)              # .../bin
        get_filename_component(_vc_root "${_vc_root}" DIRECTORY)              # .../<ver>
        get_filename_component(_vc_root "${_vc_root}" DIRECTORY)              # .../MSVC
        get_filename_component(_vc_root "${_vc_root}" DIRECTORY)              # .../Tools
        get_filename_component(_vc_root "${_vc_root}" DIRECTORY)              # .../VC

        file(GLOB _vc_crt_dirs
            "${_vc_root}/Redist/MSVC/*/${_vc_redist_arch}/Microsoft.VC*.CRT")
        if(_vc_crt_dirs)
            list(SORT _vc_crt_dirs COMPARE NATURAL)
            list(GET _vc_crt_dirs -1 _vc_crt_dir) # highest version
            file(GLOB _vc_crt_dlls "${_vc_crt_dir}/*.dll")
            install(FILES ${_vc_crt_dlls} DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL)
            message(STATUS "Bundling MSVC CRT from: ${_vc_crt_dir}")
        else()
            message(WARNING "MSVC CRT redist not found under ${_vc_root}/Redist"
                            " - the packaged app may require the VC++ redistributable")
        endif()
    endif()
endfunction()

set(CPACK_PACKAGE_VENDOR "VNoteX")
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_CONTACT "Le Tan <tamlokveer@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/COPYING.LESSER")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")

# Start menu entry on Windows
set(CPACK_PACKAGE_EXECUTABLES "vnote" "VNote")
# Desktop link on Windows
set(CPACK_CREATE_DESKTOP_LINKS "vnote")

set(CPACK_STRIP_FILES TRUE)

# WIX generator
set(CPACK_WIX_UPGRADE_GUID BA25F337-991A-4893-9D8A-AD5E89BAF5C4)
set(CPACK_WIX_PRODUCT_GUID BA25F337-991A-4893-9D8A-AD5E89BAF5C4)
set(CPACK_WIX_LICENSE_RTF "${PROJECT_SOURCE_DIR}/package/lgpl-3.0.rtf")
set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_LIST_DIR}/data/core/icons/vnote.ico")
set(CPACK_WIX_UI_BANNER "${PROJECT_SOURCE_DIR}/package/wix_banner.png")
set(CPACK_WIX_UI_DIALOG "${PROJECT_SOURCE_DIR}/package/wix_dialog.png")

#------------------------------------------------------------------------------
# include CPack, so we get target for packages
set(CPACK_OUTPUT_CONFIG_FILE "${CMAKE_BINARY_DIR}/BundleConfig.cmake")

add_custom_target(pack
                  COMMAND ${CMAKE_CPACK_COMMAND} "--config" "${CMAKE_BINARY_DIR}/BundleConfig.cmake" "--verbose"
                  COMMENT "Running CPACK. Please wait..."
                  DEPENDS vnote)
add_dependencies(pack lrelease)

set(CPACK_GENERATOR)

set(CPACK_PACKAGE_ICON "${CMAKE_CURRENT_LIST_DIR}/data/core/logo/64x64/vnote.png")

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}" DOC "Path to the windeployqt utility")

    list(APPEND CPACK_GENERATOR ZIP)
    message(STATUS "Package generation - Windows - Zip")

    if(WIN32 AND (QT_DEFAULT_MAJOR_VERSION GREATER 5))
        find_program(WIX_EXECUTABLE wix HINTS "${QT_BIN_DIR}" DOC "Path to the WiX utility")

        if (NOT WIX_EXECUTABLE-NOTFOUND)
            list(APPEND CPACK_GENERATOR WIX)
            message(STATUS "Package generation - Windows - WiX")
        endif()
    endif()

    windeployqt(vnote)
elseif(APPLE)
    # Manually copy resources. The bundle Info.plist is NOT copied here: it is
    # wired into the target through MACOSX_BUNDLE_INFO_PLIST (see
    # src/CMakeLists.txt), so the built bundle and the packaged bundle always
    # carry the same plist (including the NSServices declaration).
    set(VX_BUNDLE_CONTENTS_DIR $<TARGET_FILE_DIR:vnote>/..)
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VX_EXTRA_RESOURCE_FILES_RCC} ${VX_BUNDLE_CONTENTS_DIR}/Resources
        COMMAND ${CMAKE_COMMAND} -E make_directory ${VX_BUNDLE_CONTENTS_DIR}/Resources/translations
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VX_QM_FILES} ${VX_BUNDLE_CONTENTS_DIR}/Resources/translations
        COMMENT "Copying resources into bundle Contents ${VX_BUNDLE_CONTENTS_DIR}"
        DEPENDS vnote lrelease
    )
    add_dependencies(pack deploy)

    message(STATUS "MACDeployQtExecutable: ${MACDEPLOYQT_EXECUTABLE}")
    if (MACDEPLOYQT_EXECUTABLE)
        message(STATUS "Package generation - MacOS - DMG")

        list(APPEND CPACK_GENERATOR External)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/CPackMacDeployQt.cmake.in "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
        set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
        include(InstallRequiredSystemLibraries)
    endif()
else()
    message(STATUS "LinuxDeployExecutable: ${LINUXDEPLOY_EXECUTABLE}")
    if(LINUXDEPLOY_EXECUTABLE)
        message(STATUS "Package generation - Linux - AppImage")

        list(APPEND CPACK_GENERATOR External)
        set(VX_APPIMAGE_DEST_DIR "${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/Linux/External/AppImage")
        set(VX_APPIMAGE_DESKTOP_FILE "${VX_APPIMAGE_DEST_DIR}${CMAKE_INSTALL_PREFIX}/share/applications/vnote.desktop")
        configure_file(${CMAKE_CURRENT_LIST_DIR}/CPackLinuxDeployQt.cmake.in "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
        set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
    endif()
endif()

include(CPack)
