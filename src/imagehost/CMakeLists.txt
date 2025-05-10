# Image host provider sources are compiled in core_services (src/core/services/CMakeLists.txt).
# The vnote target links core_services, so providers are available without double-compilation.
# Headers are discovered via core_services PUBLIC include path (${CMAKE_SOURCE_DIR}/src).
