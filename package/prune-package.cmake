# Strip SDK/development artifacts from the Windows package staging tree.
#
# Run through CPACK_PRE_BUILD_SCRIPTS, i.e. after CPack has installed
# everything into its staging directory and before the ZIP / MSI is produced.
# It therefore affects the SHIPPED PACKAGE ONLY and never a plain
# `cmake --install`.
#
# Why this is needed: cmark (vendored by both vtextedit and vxcore) and
# QtKeychain declare unconditional install() rules for their headers, import
# libraries, CMake package configs, pkg-config files, qmake .pri module and, in
# cmark's case, its command-line tool. Neither project exposes an option to
# disable them, and both are consumed as submodules / FetchContent, so the rules
# cannot be turned off from this repo. Their output lands beside the application
# and is pure dead weight: VNote links cmark as a library and loads nothing from
# include/, lib/ or mkspecs/ at runtime.
#
# Keep the lists conservative and additive. Everything the application actually
# loads sits at the package root next to vnote.exe (see the "Windows packages
# install FLAT" note in the top-level CMakeLists.txt), so an entry here must be
# demonstrably not a runtime dependency.

# Directories that exist only to support building AGAINST a library.
set(_vx_prune_dirs
    bin       # duplicate MSVC CRT; see CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION
    include   # cmark + QtKeychain public headers
    lib       # import libs, CMake package configs, pkg-config files
    mkspecs   # QtKeychain qmake module (.pri)
)

# Individual files. cmark's CLI is installed by `install(TARGETS cmark_exe ...)`
# and, now that RUNTIME lands flat, sits in the package root; VNote never
# invokes it. The other root executables (vnote.exe, QtWebEngineProcess.exe) are
# both required.
set(_vx_prune_files
    cmark.exe
)

# CPack serializes the active CPACK_* variables into BOTH the binary and the
# source package configuration, so this script also runs for `package_source`.
# A source package stages the repository itself -- no vnote.exe, nothing to
# prune -- and must not trip the safety guard below. The binary configuration is
# the one that carries CPACK_INSTALL_CMAKE_PROJECTS; it is empty for source.
if(NOT CPACK_INSTALL_CMAKE_PROJECTS)
  message(STATUS "prune-package: source package staging, nothing to prune")
  return()
endif()

set(_vx_stage "${CPACK_TEMPORARY_DIRECTORY}")
if(NOT _vx_stage OR NOT EXISTS "${_vx_stage}")
  set(_vx_stage "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
endif()

if(NOT _vx_stage OR NOT EXISTS "${_vx_stage}")
  message(FATAL_ERROR
      "prune-package: cannot locate the CPack staging directory "
      "(CPACK_TEMPORARY_DIRECTORY='${CPACK_TEMPORARY_DIRECTORY}', "
      "CPACK_TEMPORARY_INSTALL_DIRECTORY='${CPACK_TEMPORARY_INSTALL_DIRECTORY}')")
endif()

# Refuse to delete anything unless this really is a VNote install root. Without
# this guard a renamed CPack variable would silently prune the wrong tree.
if(NOT EXISTS "${_vx_stage}/vnote.exe")
  message(FATAL_ERROR
      "prune-package: '${_vx_stage}' does not look like a VNote install root "
      "(no vnote.exe at its top level); refusing to delete anything")
endif()

message(STATUS "prune-package: pruning '${_vx_stage}'")

foreach(_vx_dir IN LISTS _vx_prune_dirs)
  if(EXISTS "${_vx_stage}/${_vx_dir}")
    file(GLOB_RECURSE _vx_removed LIST_DIRECTORIES false "${_vx_stage}/${_vx_dir}/*")
    list(LENGTH _vx_removed _vx_count)
    file(REMOVE_RECURSE "${_vx_stage}/${_vx_dir}")
    message(STATUS "prune-package:   removed ${_vx_dir}/ (${_vx_count} files)")
  endif()
endforeach()

foreach(_vx_file IN LISTS _vx_prune_files)
  if(EXISTS "${_vx_stage}/${_vx_file}")
    file(REMOVE "${_vx_stage}/${_vx_file}")
    message(STATUS "prune-package:   removed ${_vx_file}")
  endif()
endforeach()
