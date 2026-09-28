# Deployment-only dependency: never fetch executables during configure or app builds.
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED UPDATER_OUTPUT_DIR OR UPDATER_OUTPUT_DIR STREQUAL "")
  message(FATAL_ERROR "UPDATER_OUTPUT_DIR is required")
endif()
get_filename_component(UPDATER_OUTPUT_DIR "${UPDATER_OUTPUT_DIR}" ABSOLUTE)
file(MAKE_DIRECTORY "${UPDATER_OUTPUT_DIR}")
set(_cache "${UPDATER_OUTPUT_DIR}/cache")
file(MAKE_DIRECTORY "${_cache}")

function(download_pinned url path hash)
  if(EXISTS "${path}")
    file(SHA256 "${path}" actual)
    if(actual STREQUAL hash)
      return()
    endif()
    file(REMOVE "${path}")
  endif()
  file(DOWNLOAD "${url}" "${path}"
    EXPECTED_HASH "SHA256=${hash}" TLS_VERIFY ON STATUS status
    TIMEOUT 180 INACTIVITY_TIMEOUT 30)
  list(GET status 0 code)
  list(GET status 1 detail)
  if(NOT code EQUAL 0)
    file(REMOVE "${path}")
    message(FATAL_ERROR "Pinned updater dependency download failed: ${detail}")
  endif()
endfunction()

download_pinned(
  "https://github.com/jedisct1/minisign/releases/download/0.11/minisign-0.11-win64.zip"
  "${_cache}/minisign-0.11-win64.zip"
  "b9c31c2c3034f81f0e5f5d92cbcc20e67a9671b6e5455661588638848dc58031")
download_pinned(
  "https://raw.githubusercontent.com/jedisct1/minisign/0.11/LICENSE"
  "${_cache}/LICENSE.minisign"
  "d775d155cbf31638714c31c6f990f9fdc5f07998d91e42d1bf15483bd2d1706b")
# The pinned archive contains exactly this one member. Extract in the build tree.
set(_extract "${_cache}/extracted")
file(MAKE_DIRECTORY "${_extract}")
file(ARCHIVE_EXTRACT INPUT "${_cache}/minisign-0.11-win64.zip"
  DESTINATION "${_extract}" PATTERNS "minisign-win64/minisign.exe")
set(_exe "${_extract}/minisign-win64/minisign.exe")
if(NOT EXISTS "${_exe}")
  message(FATAL_ERROR "Pinned minisign archive lacks minisign-win64/minisign.exe")
endif()
file(SHA256 "${_exe}" _hash)
if(NOT _hash STREQUAL "6537b1da726d593877dc21720d8f8c44e6c7485da3dfddddee73e8b457e49b1a")
  message(FATAL_ERROR "Pinned minisign executable hash mismatch")
endif()
configure_file("${_exe}" "${UPDATER_OUTPUT_DIR}/minisign.exe" COPYONLY)
configure_file("${_cache}/LICENSE.minisign" "${UPDATER_OUTPUT_DIR}/LICENSE.minisign" COPYONLY)
message(STATUS "Prepared pinned minisign 0.11 in ${UPDATER_OUTPUT_DIR}")
