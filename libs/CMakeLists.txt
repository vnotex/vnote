add_subdirectory(QHotkey)

# ORDER IS LOAD-BEARING: vtextedit must be added before vxcore.
#
# The cmark fork is vendored TWICE -- as libs/vtextedit/libs/cmark and as
# libs/vxcore/third_party/cmark. The two are NOT symmetric:
# libs/vtextedit/libs/CMakeLists.txt calls add_subdirectory(cmark)
# UNCONDITIONALLY (no if(NOT TARGET cmark) guard), while
# libs/vxcore/third_party/CMakeLists.txt guards its own copy with
# `if(NOT TARGET cmark)`. With vtextedit first, vtextedit's copy is the one
# compiled, vxcore's guard sees the target already exists and skips its copy,
# and vxcore links vtextedit's cmark.
#
# Consequence: both pins MUST stay identical, or vxcore is silently built
# against a cmark it was never tested with (its standalone build,
# libs/vxcore/build_test and ci-linux-tsan.yml, uses its own copy). That is
# enforced by tests/utils/test_cmark_pin_drift.cpp.
#
# Swapping these two add_subdirectory() calls does NOT simply flip which copy
# wins: vxcore would define `cmark` first, then vtextedit's unguarded
# add_subdirectory(cmark) would try to define the same target again and CMake
# would fail at CONFIGURE time. Making that order work would require adding a
# guard inside the vtextedit submodule, and would additionally move vxcore's
# set(CMAKE_MSVC_RUNTIME_LIBRARY ... FORCE) (libs/vxcore/third_party/CMakeLists.txt)
# ahead of vtextedit's configure, applying it to syntax-highlighting, katevi,
# sonnet, hunspell and VTextEdit. Do not reorder.
add_subdirectory(vtextedit)

# vxcore - VNote core library (notebook management, tags, search)
# Disable building tests and CLI when used as a submodule
set(VXCORE_BUILD_TESTS OFF CACHE BOOL "Disable vxcore tests" FORCE)
set(VXCORE_BUILD_CLI OFF CACHE BOOL "Disable vxcore CLI" FORCE)
set(VXCORE_BUILD_SHARED OFF CACHE BOOL "Build vxcore as static library" FORCE)
set(VXCORE_INSTALL OFF CACHE BOOL "Disable vxcore install targets" FORCE)
add_subdirectory(vxcore)

# qwindowkit - Frameless window support
set(QWINDOWKIT_BUILD_STATIC ON CACHE BOOL "Build qwindowkit as static library" FORCE)
set(QWINDOWKIT_BUILD_WIDGETS ON CACHE BOOL "Build qwindowkit Widgets module" FORCE)
set(QWINDOWKIT_BUILD_QUICK OFF CACHE BOOL "Disable qwindowkit Quick module" FORCE)
set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "Disable qwindowkit examples" FORCE)
set(QWINDOWKIT_INSTALL OFF CACHE BOOL "Disable qwindowkit install targets" FORCE)
set(QWINDOWKIT_ENABLE_WINDOWS_SYSTEM_BORDERS ON CACHE BOOL "Enable Win10/11 native top border" FORCE)
add_subdirectory(qwindowkit)
