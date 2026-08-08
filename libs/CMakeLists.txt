add_subdirectory(QHotkey)
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
