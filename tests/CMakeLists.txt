# =============================================================================
# VNote Test Infrastructure
# =============================================================================
# Early-return guard: skip entire tests subdirectory if VNOTE_BUILD_TESTS is OFF
if(NOT VNOTE_BUILD_TESTS)
  return()
endif()
#
# Usage:
#   add_qt_test(test_name
#     SOURCES source1.cpp source2.cpp ...
#     [GUILESS]  # Test needs no QtWidgets: do NOT link Qt6::Widgets
#   )
#
# NOTE: GUILESS is a LINK-LEVEL switch only. It cannot and does not choose
# between QTEST_MAIN and QTEST_GUILESS_MAIN - that is a source-level macro
# decision made in the test .cpp itself. A GUILESS target that legitimately
# includes a QtWidgets header (without needing a QApplication) should list
# Qt6::Widgets explicitly in LINKS.
#
# Examples:
#   add_qt_test(test_error SOURCES test_error.cpp ${CMAKE_SOURCE_DIR}/src/core/error.cpp GUILESS)
#   add_qt_test(test_widget SOURCES test_widget.cpp)  # Links Qt6::Widgets
#
# =============================================================================

find_package(Qt6 REQUIRED COMPONENTS Core Test Gui Widgets Concurrent)
find_package(Qt6 OPTIONAL_COMPONENTS Core5Compat)

# Helper function to create a test executable
function(add_qt_test NAME)
  cmake_parse_arguments(ARG "GUILESS" "" "SOURCES;LINKS" ${ARGN})

  add_executable(${NAME} ${ARG_SOURCES})

  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/tests/helpers
    ${CMAKE_SOURCE_DIR}/libs/vxcore/include
  )

  target_link_libraries(${NAME} PRIVATE
    Qt6::Core
    Qt6::Test
    ${ARG_LINKS}
  )

  # Non-GUILESS targets need QtWidgets. This used to arrive transitively through
  # core_services -> VTextEdit -> Qt::Widgets (PUBLIC); that link was deliberately
  # severed, so the dependency is declared here instead. See the load-bearing
  # comment in src/core/services/CMakeLists.txt.
  if(NOT ARG_GUILESS)
    target_link_libraries(${NAME} PRIVATE Qt6::Gui Qt6::Widgets)
  endif()

  # Set C++ standard
  target_compile_features(${NAME} PRIVATE cxx_std_14)

  # Add the test to CTest
  add_test(NAME ${NAME} COMMAND ${NAME})

  # Set test properties
  set_tests_properties(${NAME} PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
endfunction()

# Tests are never shipped; they run only on the CI runner's native arch. The
# app is built universal (-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"), but a
# universal test build forces an x86_64 link of moc-emitted vtables (e.g.
# ViewWindow2) whose out-of-line destructors are intentionally not compiled into
# stubbed test translation units -> the x86_64 link fails (arm64 succeeds),
# aborting `make all` before the widgets/integration tests build. Narrowing the
# tests to the host arch eliminates that entire failure class while keeping the
# shipped app universal. Directory-scoped: inherited by every target created in
# tests/ and its subdirectories (all route through add_qt_test -> add_executable
# above). No effect on Linux/Windows (OSX_ARCHITECTURES is Apple-only).
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

# Subdirectories
add_subdirectory(helpers)
add_subdirectory(core)
add_subdirectory(utils)
add_subdirectory(models)
add_subdirectory(controllers)
add_subdirectory(widgets)
add_subdirectory(gui)
add_subdirectory(export)
add_subdirectory(unitedentry)
add_subdirectory(integration)
