add_subdirectory(QHotkey)
add_subdirectory(vtextedit)

# vxcore - VNote core library (notebook management, tags, search)
# Disable building tests and CLI when used as a submodule
set(VXCORE_BUILD_TESTS OFF CACHE BOOL "Disable vxcore tests" FORCE)
set(VXCORE_BUILD_CLI OFF CACHE BOOL "Disable vxcore CLI" FORCE)
set(VXCORE_BUILD_SHARED OFF CACHE BOOL "Build vxcore as static library" FORCE)
set(VXCORE_INSTALL OFF CACHE BOOL "Disable vxcore install targets" FORCE)
add_subdirectory(vxcore)
