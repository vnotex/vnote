cmake_minimum_required (VERSION 3.20)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.1" CACHE STRING "Minimum OS X deployment version")

project(VNote
    VERSION 4.5.0
    DESCRIPTION "A pleasant note-taking platform"
    HOMEPAGE_URL "https://app.vnote.fun"
    LANGUAGES C CXX)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type, defaults to Release")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The macOS NSServices provider lives inside src/application.cpp, which is
# compiled as Objective-C++ on Apple only (see src/CMakeLists.txt). Enabling the
# language here keeps every other platform on a plain C/CXX project.
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "Qt version to use (5 or 6), defaults to 6")

# Windows packages install FLAT: the release ZIP's single top-level directory IS
# the install root, with vnote.exe and every DLL directly beneath it. An extra
# "bin/" level would break that layout -- the packaging scripts, the published
# manifests and every "unzip over the old folder" upgrade all assume the runtime
# lives directly under the package root.
#
# This MUST be set before add_subdirectory(libs): vxcore, qwindowkit and QHotkey
# each include(GNUInstallDirs), which caches CMAKE_INSTALL_BINDIR as "bin". A
# plain (non-cache) variable here is already "DEFINED", so GNUInstallDirs leaves
# it alone and every install(DESTINATION ${CMAKE_INSTALL_BINDIR}) below -- ours
# AND the submodules' (e.g. VTextEdit.dll) -- resolves flat. Setting it later,
# or as a non-FORCE cache entry, is silently ignored.
#
# Linux/macOS keep the GNUInstallDirs default ("bin"), which the AppImage and
# the .app bundle layouts require.
if(WIN32)
  set(CMAKE_INSTALL_BINDIR ".")

  # Same reasoning, and it MUST also be set before add_subdirectory(libs):
  # cmark (vendored by both vtextedit and vxcore) calls
  # include(InstallRequiredSystemLibraries) itself without pinning a
  # destination, and that module defaults to "bin" on Windows. Because cmark is
  # processed before src/, it shipped a SECOND, byte-identical copy of the eight
  # MSVC CRT DLLs under bin/ in 4.4.2. Pre-setting the variable makes the
  # module's `if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION)` fall through, so
  # every include of it -- ours and cmark's -- targets the same flat root.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

set(QHOTKEY_INSTALL OFF CACHE BOOL "Disable installing QHotKey" FORCE)
add_subdirectory(libs)

# -----------------------------------------------------------------------------
# QtKeychain (secure credential storage for git sync PAT)
# -----------------------------------------------------------------------------
option(VNOTE_USE_KEYCHAIN "Build with QtKeychain for secure credential storage" ON)

set(VNOTE_KEYCHAIN_AVAILABLE FALSE)
    if(VNOTE_USE_KEYCHAIN)
    # Select QtKeychain variant (Qt5 or Qt6) based on QT_DEFAULT_MAJOR_VERSION.
    if(QT_DEFAULT_MAJOR_VERSION GREATER 5)
        set(BUILD_WITH_QT6 ON CACHE BOOL "Build QtKeychain with Qt6" FORCE)
        find_package(Qt6Keychain QUIET)
    else()
        set(BUILD_WITH_QT6 OFF CACHE BOOL "Build QtKeychain with Qt5" FORCE)
        find_package(Qt5Keychain QUIET)
    endif()

    if(Qt6Keychain_FOUND OR Qt5Keychain_FOUND)
        set(VNOTE_KEYCHAIN_AVAILABLE TRUE)
        if(QT_DEFAULT_MAJOR_VERSION GREATER 5)
            message(STATUS "QtKeychain: found system Qt6Keychain (target Qt6Keychain::Qt6Keychain)")
        else()
            message(STATUS "QtKeychain: found system Qt5Keychain (target Qt5Keychain::Qt5Keychain)")
        endif()
    else()
        message(STATUS "QtKeychain: system QtKeychain not found, falling back to FetchContent (v0.14.3)")
        include(FetchContent)
        # Configure QtKeychain build options before population.
        if(QT_DEFAULT_MAJOR_VERSION GREATER 5)
            set(BUILD_WITH_QT6 ON CACHE BOOL "Build QtKeychain with Qt6" FORCE)
        else()
            set(BUILD_WITH_QT6 OFF CACHE BOOL "Build QtKeychain with Qt5" FORCE)
        endif()
        set(BUILD_TEST_APPLICATION OFF CACHE BOOL "Disable QtKeychain test app" FORCE)
        set(BUILD_TRANSLATIONS OFF CACHE BOOL "Disable QtKeychain translations" FORCE)
        FetchContent_Declare(
            qtkeychain
            GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain
            GIT_TAG        0.14.3
        )
        FetchContent_MakeAvailable(qtkeychain)
        if(TARGET Qt6Keychain OR TARGET qt6keychain OR TARGET Qt5Keychain OR TARGET qt5keychain)
            set(VNOTE_KEYCHAIN_AVAILABLE TRUE)
            message(STATUS "QtKeychain: fetched and built v0.14.3")
        else()
            message(WARNING "QtKeychain: FetchContent did not produce expected target; sync features will be non-functional at runtime")
        endif()
    endif()
else()
    message(STATUS "QtKeychain: VNOTE_USE_KEYCHAIN=OFF, sync features will be non-functional at runtime (per ADR-9, no plaintext fallback)")
endif()

add_subdirectory(src)

# tlsprobe: Windows-only CI gate proving the packaged build can initialize TLS.
# Deliberately in the default ALL target (so the Qt5 job's plain `cmake --build .`
# produces it) and independent of VNOTE_BUILD_TESTS, which that job sets to OFF.
if(WIN32)
    add_subdirectory(tools/tlsprobe)
endif()

# TODO: find a better way to organize tests
# OFF by default: a plain `cmake ..` builds only the app. CI turns it ON explicitly
# (see .github/workflows/ci-linux.yml, ci-macos.yml, ci-win.yml).
option(VNOTE_BUILD_TESTS "Build VNote unit tests" OFF)

enable_testing()
if(VNOTE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
