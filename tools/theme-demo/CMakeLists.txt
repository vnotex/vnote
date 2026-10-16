# Qt imported targets discovered under src/ are directory-scoped.
find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Svg WebEngineWidgets Test)

add_executable(vnote-theme-demo
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/core/theme.cpp)

target_link_libraries(vnote-theme-demo PRIVATE
    core_services
    VTextEdit
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::WebEngineWidgets
    Qt6::Test)

target_compile_definitions(vnote-theme-demo PRIVATE
    VNOTE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

set(_demo_output "${CMAKE_BINARY_DIR}/tools/theme-demo")
set_target_properties(vnote-theme-demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_demo_output}")
foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(vnote-theme-demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_config} "${_demo_output}")
endforeach()

# A console executable and build-tree-only launcher: no deployment or install.
file(GENERATE OUTPUT "${_demo_output}/run-theme-demo-$<CONFIG>.ps1" CONTENT
"$oldPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent '$<TARGET_FILE:Qt6::Core>') + ';' + (Split-Path -Parent '$<TARGET_FILE:VTextEdit>') + ';' + (Split-Path -Parent '$<TARGET_FILE:vxcore>') + ';' + $oldPath
    & '$<TARGET_FILE:vnote-theme-demo>' @args
    $demoExitCode = $LASTEXITCODE
} finally {
    $env:PATH = $oldPath
}
exit $demoExitCode
")

# Multi-config generators must not generate different DLL paths into one file.
# Publish the launcher matching the executable that was actually just built.
add_custom_command(TARGET vnote-theme-demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_demo_output}/run-theme-demo-$<CONFIG>.ps1"
        "${_demo_output}/run-theme-demo.ps1"
    VERBATIM)
