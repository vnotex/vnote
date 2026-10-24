cmake_minimum_required(VERSION 3.21)

# The macOS package is the build bundle, not CPack's install staging tree.
# Run after deployment/fixup and before signing; VERIFY_ONLY checks the signed DMG.
get_filename_component(_vx_app "${VNOTE_APP_DIR}" ABSOLUTE)
set(_vx_contents "${_vx_app}/Contents")
if(NOT VNOTE_APP_DIR OR NOT EXISTS "${_vx_contents}/MacOS/VNote"
    OR IS_DIRECTORY "${_vx_contents}/MacOS/VNote")
    message(FATAL_ERROR "Not a VNote app bundle: '${VNOTE_APP_DIR}'; refusing to prune")
endif()

set(_vx_locales
    "${_vx_contents}/Frameworks/QtWebEngineCore.framework/Versions/A/Resources/qtwebengine_locales")
set(_vx_keep_locales en-US.pak zh-CN.pak ja.pak ko.pak)

# Validate all retained packs before deleting anything. Qt falls back to en-US
# for unsupported locales; application locale/date formatting stays unchanged.
foreach(_vx_name IN LISTS _vx_keep_locales)
    set(_vx_path "${_vx_locales}/${_vx_name}")
    if(NOT EXISTS "${_vx_path}" OR IS_DIRECTORY "${_vx_path}")
        message(FATAL_ERROR "Missing required WebEngine locale: ${_vx_path}")
    endif()
    file(SIZE "${_vx_path}" _vx_size)
    if(_vx_size EQUAL 0)
        message(FATAL_ERROR "Empty required WebEngine locale: ${_vx_path}")
    endif()
endforeach()

# Do not silently keep another SDK locale tree. Framework aliases are not traversed.
file(GLOB_RECURSE _vx_entries LIST_DIRECTORIES true "${_vx_contents}/*")
foreach(_vx_entry IN LISTS _vx_entries)
    if(_vx_entry MATCHES "/qtwebengine_locales$" AND NOT _vx_entry STREQUAL _vx_locales)
        message(FATAL_ERROR "Unexpected WebEngine locale directory: ${_vx_entry}")
    endif()
endforeach()

# Keep QtPdf (Typst), QtPositioning and the other positioning backends, TLS,
# QML/Quick, WebEngine/DevTools resources, both architecture slices and licenses.
set(_vx_remove
    PlugIns/imageformats/libqpdf.dylib
    PlugIns/imageformats/libqpdf_debug.dylib
    PlugIns/position/libqtposition_nmea.dylib
    PlugIns/position/libqtposition_nmea_debug.dylib
    PlugIns/sqldrivers
    Frameworks/QtSql.framework
    Frameworks/QtSerialPort.framework
)
foreach(_vx_relative IN LISTS _vx_remove)
    set(_vx_path "${_vx_contents}/${_vx_relative}")
    if(NOT VERIFY_ONLY)
        file(REMOVE_RECURSE "${_vx_path}")
    endif()
    if(EXISTS "${_vx_path}" OR IS_SYMLINK "${_vx_path}")
        message(FATAL_ERROR "Unneeded Qt component in macOS package: ${_vx_path}")
    endif()
endforeach()

file(GLOB _vx_paks "${_vx_locales}/*.pak")
foreach(_vx_path IN LISTS _vx_paks)
    get_filename_component(_vx_name "${_vx_path}" NAME)
    if(NOT _vx_name IN_LIST _vx_keep_locales)
        if(NOT VERIFY_ONLY)
            file(REMOVE "${_vx_path}")
        endif()
        if(EXISTS "${_vx_path}" OR IS_SYMLINK "${_vx_path}")
            message(FATAL_ERROR "Unneeded WebEngine locale in macOS package: ${_vx_path}")
        endif()
    endif()
endforeach()

message(STATUS "macOS package policy: no PDF image/NMEA/Sql/SerialPort components; locales en-US, zh-CN, ja, ko")
