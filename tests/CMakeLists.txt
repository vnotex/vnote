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

find_package(Qt6 REQUIRED COMPONENTS Core Test Gui Widgets Concurrent Svg)
find_package(Qt6 OPTIONAL_COMPONENTS Core5Compat)
# QJSEngine, used by test_pdfviewercore_js to evaluate the shipped pdf.js glue
# scripts. Optional so a Qt install without qtdeclarative still configures; CI
# asserts the test is registered so a missing module cannot silently drop the
# only gate covering the JS outline contract.
find_package(Qt6 OPTIONAL_COMPONENTS Qml)

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

# =============================================================================
# macOS Services metadata (Create Note in VNote)
# =============================================================================
# Asserted against the FINAL BUNDLE plist, not the source file: the bundle is
# what macOS actually reads, and it is produced through MACOSX_BUNDLE_INFO_PLIST
# (src/CMakeLists.txt). Exact raw values and types are pinned, because a
# silently mistyped key (e.g. NSRestricted as a string) still lints clean while
# breaking Service discovery.
if(APPLE)
  set(VX_BUNDLE_PLIST "$<TARGET_BUNDLE_DIR:vnote>/Contents/Info.plist")

  # Helper: assert one plist key resolves to one exact raw value.
  function(add_service_plist_test NAME KEYPATH EXPECTED)
    add_test(NAME test_macos_service_plist_${NAME}
      COMMAND plutil -extract "${KEYPATH}" raw -o - "${VX_BUNDLE_PLIST}")
    set_tests_properties(test_macos_service_plist_${NAME} PROPERTIES
      PASS_REGULAR_EXPRESSION "^${EXPECTED}\n?$"
      TIMEOUT 60
    )
  endfunction()

  add_service_plist_test(nsmessage "NSServices.0.NSMessage" "createNoteFromSelection")
  add_service_plist_test(nsportname "NSServices.0.NSPortName" "VNote")
  add_service_plist_test(nsmenuitem_default "NSServices.0.NSMenuItem.default"
                         "Create Note in VNote")
  add_service_plist_test(nssendtypes_0 "NSServices.0.NSSendTypes.0"
                         "public.utf8-plain-text")
  # Boolean, not the string "false": a mistyped value still lints clean.
  add_service_plist_test(nsrestricted "NSServices.0.NSRestricted" "false")

  # NSRequiredContext must be a dictionary (an empty one), and NSReturnTypes
  # must be ABSENT because the Service returns nothing.
  add_test(NAME test_macos_service_plist_nsrequiredcontext_is_dict
    COMMAND /usr/libexec/PlistBuddy -c "Print :NSServices:0:NSRequiredContext"
            "${VX_BUNDLE_PLIST}")
  set_tests_properties(test_macos_service_plist_nsrequiredcontext_is_dict PROPERTIES
    PASS_REGULAR_EXPRESSION "Dict"
    TIMEOUT 60
  )

  add_test(NAME test_macos_service_plist_no_nsreturntypes
    COMMAND /usr/libexec/PlistBuddy -c "Print :NSServices:0:NSReturnTypes"
            "${VX_BUNDLE_PLIST}")
  set_tests_properties(test_macos_service_plist_no_nsreturntypes PROPERTIES
    WILL_FAIL TRUE
    TIMEOUT 60
  )
endif()
