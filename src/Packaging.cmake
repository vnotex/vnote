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
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/winqt/styles/"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/winqt/qmltooling/"
        COMMENT "Deploying Qt..."
        DEPENDS vnote
    )

    add_dependencies(deploy lrelease)
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
    include(InstallRequiredSystemLibraries)
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
                  COMMAND ${CMAKE_CPACK_COMMAND} "--config" "${CMAKE_BINARY_DIR}/BundleConfig.cmake"
                  COMMENT "Running CPACK. Please wait..."
                  DEPENDS vnote)
add_dependencies(pack lrelease)

set(CPACK_GENERATOR)

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}" DOC "Path to the windeployqt utility")

    list(APPEND CPACK_GENERATOR ZIP)
    message(STATUS "Package generation - Windows - Zip")

    find_program(WIX_EXECUTABLE wix HINTS "${QT_BIN_DIR}" DOC "Path to the WiX utility")

    if (NOT WIX_EXECUTABLE-NOTFOUND)
        list(APPEND CPACK_GENERATOR WIX)
        message(STATUS "Package generation - Windows - WiX")
    endif()

    windeployqt(vnote)
elseif(APPLE)
else()
    message(STATUS "LinuxDeployExecutable: ${LINUXDEPLOY_EXECUTABLE}")
    if(LINUXDEPLOY_EXECUTABLE)
        message(STATUS "Package generation - Linux - AppImage")

        set(CPACK_GENERATOR "External;${CPACK_GENERATOR}")
        set(VX_APPIMAGE_DEST_DIR "${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/Linux/External/AppImage")
        set(VX_APPIMAGE_DESKTOP_FILE "${VX_APPIMAGE_DEST_DIR}${CMAKE_INSTALL_PREFIX}/share/applications/vnote.desktop")
        configure_file(${CMAKE_CURRENT_LIST_DIR}/CPackLinuxDeployQt.cmake.in "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
        set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_BINARY_DIR}/CPackExternal.cmake")
    endif()

    set(CPACK_PACKAGE_ICON "${CMAKE_CURRENT_LIST_DIR}/data/core/logo/64x64/vnote.png")
endif()

include(CPack)
