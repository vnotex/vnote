# Utils module tests
add_qt_test(test_autostartutils
  SOURCES
    test_autostartutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/autostartutils.cpp
  GUILESS
)

add_qt_test(test_pathutils
  SOURCES
    test_pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
  LINKS
    Qt6::Gui
  GUILESS
)

add_qt_test(test_htmlutils
  SOURCES
    test_htmlutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/htmlutils.cpp
  GUILESS
)

add_qt_test(test_fileutils2
  SOURCES
    test_fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Gui
  GUILESS
)

add_qt_test(test_scrollpreservationpolicy
  SOURCES
    test_scrollpreservationpolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/scrollpreservationpolicy.cpp
  GUILESS
)

add_qt_test(test_cursorpreservationpolicy
  SOURCES
    test_cursorpreservationpolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/cursorpreservationpolicy.cpp
  GUILESS
)

add_qt_test(test_headingslug
  SOURCES
    test_headingslug.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/headingslugger.cpp
  GUILESS
)

add_qt_test(test_stringutils
  SOURCES
    test_stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/strnatcmp.c
  GUILESS
)

# The behavioral half of this test executes the injected JavaScript in a QJSEngine, so it needs
# Qt6::Qml, which tests/CMakeLists.txt finds as an OPTIONAL component.
if(TARGET Qt6::Qml)
  add_qt_test(test_wkhtmltopdfhtmlpatch
    SOURCES
      test_wkhtmltopdfhtmlpatch.cpp
      ${CMAKE_SOURCE_DIR}/src/export/wkhtmltopdfhtmlpatch.cpp
    LINKS
      Qt6::Gui
      Qt6::Qml
    GUILESS
  )
endif()

add_qt_test(test_exportfontresolver
  SOURCES
    test_exportfontresolver.cpp
    ${CMAKE_SOURCE_DIR}/src/export/exportfontresolver.cpp
  LINKS
    Qt6::Gui
  GUILESS
)

add_qt_test(test_clipboard_image
  SOURCES
    test_clipboard_image.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/clipboardutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/global.cpp
  LINKS
    Qt6::Gui
    Qt6::Widgets
    VTextEdit
  GUILESS
)

add_qt_test(test_urlutils
  SOURCES
    test_urlutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/urlutils.cpp
  GUILESS
)

# copyMarkdownMediaFiles() is what fills an exported bundle. Two behaviours are
# easy to lose silently: a reference-style image must still be copied (it has no
# destination span, and the old text-search implementation dropped it), and a
# rename must rewrite the destination by its RAW span rather than the cleaned
# url's length. See tests/utils/test_contentmediautils.cpp.
add_qt_test(test_contentmediautils
  SOURCES
    test_contentmediautils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/contentmediautils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/imagedestinationrewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/global.cpp
    # Only reached by copyMediaFiles(), which the tests do not call; needed to
    # link the translation unit.
    ${CMAKE_SOURCE_DIR}/src/core/buffer/filetypehelper.cpp
    ${CMAKE_SOURCE_DIR}/src/core/services/filetype.cpp
    ${CMAKE_SOURCE_DIR}/src/core/coreconfig.cpp
  LINKS
    Qt6::Gui
    VTextEdit
  GUILESS
)

add_qt_test(test_fileutils2_staging
  SOURCES
    test_fileutils2_staging.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/fileutils2.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/pathutils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
  LINKS
    Qt6::Gui
  GUILESS
)

# Grep-gate regression test: prevents reintroduction of shared-schema JSON
# key literals in src/. See tests/utils/test_json_key_drift.cpp for the full
# contract (gated literal set, allow-list, escape hatch).
add_qt_test(test_json_key_drift
  SOURCES
    test_json_key_drift.cpp
  GUILESS
)
target_compile_definitions(test_json_key_drift PRIVATE
  VNOTE_SRC_DIR="${CMAKE_SOURCE_DIR}/src"
)

# Grep-gate regression test: VNote ships 10 themes (6 dark), so a hardcoded
# color in a setStyleSheet() call is only correct in whichever theme its author
# happened to run. See tests/utils/test_hardcoded_color_drift.cpp for the full
# contract (what is detected, allow-list, escape hatch).
add_qt_test(test_hardcoded_color_drift
  SOURCES
    test_hardcoded_color_drift.cpp
  LINKS
    # QColor::colorNames() supplies the full SVG color-name set, so the
    # detector cannot fall behind a hand-written list. Static call; no
    # QGuiApplication needed, hence still GUILESS.
    Qt6::Gui
  GUILESS
)
target_compile_definitions(test_hardcoded_color_drift PRIVATE
  VNOTE_SRC_DIR="${CMAKE_SOURCE_DIR}/src"
)

# Grep-gate regression test: item indentation must come from the one shared
# helper (vnotex::TreeViewUtils), or the Notebooks / Tags / Search / Outline /
# Tasks dock trees drift apart again. Qt style sheets have no `indentation`
# property, so this cannot be enforced in interface.qss.
add_qt_test(test_tree_indentation_drift
  SOURCES
    test_tree_indentation_drift.cpp
  GUILESS
)
target_compile_definitions(test_tree_indentation_drift PRIVATE
  VNOTE_SRC_DIR="${CMAKE_SOURCE_DIR}/src"
)

# Grep-gate regression test: no regular-expression Markdown image parser may
# come back. vtextedit 2d07683 + vnote 5f811d45 deleted the `=WxH` group from
# such an expression and thereby stopped it matching sized images at all,
# costing three and a half months of broken in-place previews. The defect was
# the EXISTENCE of a second parser, not any one line of it, so only a gate on
# its existence catches the next one. See tests/utils/test_image_parser_drift.cpp.
add_qt_test(test_image_parser_drift
  SOURCES
    test_image_parser_drift.cpp
  GUILESS
)
target_compile_definitions(test_image_parser_drift PRIVATE
  VNOTE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Grep-gate regression test: the vendored markdown-it-texmath block rule must
# push its token with nesting 0. Upstream pushes 1 without a closing token, so
# every math block permanently inflates state.level and markdown-it's block
# tokenizer silently truncates the document at options.maxNesting (default 100)
# -- a note with 101 formulas lost everything after the 100th. Also gates the
# explicit `maxNesting: 500` in markdownit.js. See
# tests/utils/test_texmath_nesting_drift.cpp.
add_qt_test(test_texmath_nesting_drift
  SOURCES
    test_texmath_nesting_drift.cpp
  GUILESS
)
target_compile_definitions(test_texmath_nesting_drift PRIVATE
  VNOTE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# The `src`-rewriting half of WebViewExporter, extracted so the guarantee that a
# sized image keeps its width/height across an export is testable without a web
# engine.
add_qt_test(test_imgsrcrewriter
  SOURCES
    test_imgsrcrewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/export/imgsrcrewriter.cpp
  GUILESS
)
target_include_directories(test_imgsrcrewriter PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Coverage gate between the vendored pdf.js tree on disk and the pdf.js block of
# src/data/extra/extra.qrc. A file with no qrc entry 404s silently at runtime
# inside the WebEngine page; nothing else in the build notices. See
# tests/utils/test_extra_qrc_coverage.cpp for the runtime-subset definition.
add_qt_test(test_extra_qrc_coverage
  SOURCES
    test_extra_qrc_coverage.cpp
  GUILESS
)
target_compile_definitions(test_extra_qrc_coverage PRIVATE
  VNOTE_SRC_DIR="${CMAKE_SOURCE_DIR}/src"
)

# Drift gate between the two vendored cmark submodules. Only vtextedit's copy
# is ever compiled: it is added first in libs/CMakeLists.txt AND is the
# unguarded one, so vxcore's if(NOT TARGET cmark) guard skips its own copy and
# vxcore links vtextedit's cmark. Divergent pins would therefore compile vxcore
# against a cmark it was never tested with. See libs/CMakeLists.txt and
# tests/utils/test_cmark_pin_drift.cpp for the full contract (the two vendoring
# sites are NOT symmetric -- do not read this as "first one configured wins").
add_qt_test(test_cmark_pin_drift
  SOURCES
    test_cmark_pin_drift.cpp
  GUILESS
)
target_compile_definitions(test_cmark_pin_drift PRIVATE
  VNOTE_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)

target_include_directories(test_clipboard_image PRIVATE
  ${CMAKE_SOURCE_DIR}/libs/vtextedit/src/include
)

# Copy VTextEdit DLL next to test executable so CTest can find it at runtime.
add_custom_command(TARGET test_clipboard_image POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_clipboard_image>
)
