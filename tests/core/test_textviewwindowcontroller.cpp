#include <QtTest>

#include <vtextedit/global.h>
#include <vtextedit/texteditorconfig.h>
#include <vtextedit/viconfig.h>
#include <vtextedit/vtexteditor.h>

#include <controllers/textviewwindowcontroller.h>
#include <core/editorconfig.h>
#include <core/nodeidentifier.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/texteditorconfig.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestTextViewWindowController : public QObject {
  Q_OBJECT

private slots:
  // ============ Group 1: buildTextEditorConfig ============

  // Line number type mapping
  void testBuildConfig_lineNumberAbsolute();
  void testBuildConfig_lineNumberRelative();
  void testBuildConfig_lineNumberNone();

  // Input mode mapping
  void testBuildConfig_inputModeNormal();
  void testBuildConfig_inputModeVi();
  void testBuildConfig_inputModeVscode();

  // Wrap mode mapping
  void testBuildConfig_wrapModeNoWrap();
  void testBuildConfig_wrapModeWordWrap();
  void testBuildConfig_wrapModeWrapAnywhere();
  void testBuildConfig_wrapModeWordWrapOrAnywhere();

  // Center cursor mapping
  void testBuildConfig_centerCursorNever();
  void testBuildConfig_centerCursorAlways();
  void testBuildConfig_centerCursorOnBottom();

  // Line ending policy mapping
  void testBuildConfig_lineEndingPlatform();
  void testBuildConfig_lineEndingFile();
  void testBuildConfig_lineEndingLF();
  void testBuildConfig_lineEndingCRLF();
  void testBuildConfig_lineEndingCR();

  // Boolean fields
  void testBuildConfig_expandTab();
  void testBuildConfig_textFolding();
  void testBuildConfig_highlightWhitespace();

  // Integer fields
  void testBuildConfig_tabStopWidth();

  // Scale factor
  void testBuildConfig_scaleFactor();

  // Theme strings
  void testBuildConfig_emptyThemeFile();
  void testBuildConfig_syntaxTheme();

  // ViConfig forwarding
  void testBuildConfig_viConfigForwarded();

  // ============ Group 2: buildTextEditorParameters ============

  void testBuildParams_spellCheckEnabled();
  void testBuildParams_autoDetectLanguage();
  void testBuildParams_defaultSpellCheckLanguage();

  // ============ Group 3: prepareBufferState ============

  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testPrepareBufferState_invalidBuffer();
  void testPrepareBufferState_validBuffer();
  void testPrepareBufferState_modifiedBuffer();
  void testPrepareBufferState_txtExtension();

private:
  // Helpers for Group 1/2 — create default EditorConfig with null mgr/parent.
  EditorConfig makeEditorConfig() { return EditorConfig(nullptr, nullptr); }

  // Group 3 members
  VxCoreContextHandle m_context = nullptr;
  BufferService *m_bufferService = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

// ============ Group 1: buildTextEditorConfig ============

// --- Line number type ---

void TestTextViewWindowController::testBuildConfig_lineNumberAbsolute() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setLineNumberType(TextEditorConfig::LineNumberType::Absolute);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_lineNumberType, vte::VTextEditor::LineNumberType::Absolute);
}

void TestTextViewWindowController::testBuildConfig_lineNumberRelative() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setLineNumberType(TextEditorConfig::LineNumberType::Relative);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_lineNumberType, vte::VTextEditor::LineNumberType::Relative);
}

void TestTextViewWindowController::testBuildConfig_lineNumberNone() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setLineNumberType(TextEditorConfig::LineNumberType::None);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_lineNumberType, vte::VTextEditor::LineNumberType::None);
}

// --- Input mode ---

void TestTextViewWindowController::testBuildConfig_inputModeNormal() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setInputMode(TextEditorConfig::InputMode::NormalMode);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_inputMode, vte::InputMode::NormalMode);
}

void TestTextViewWindowController::testBuildConfig_inputModeVi() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setInputMode(TextEditorConfig::InputMode::ViMode);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_inputMode, vte::InputMode::ViMode);
}

void TestTextViewWindowController::testBuildConfig_inputModeVscode() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setInputMode(TextEditorConfig::InputMode::VscodeMode);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_inputMode, vte::InputMode::VscodeMode);
}

// --- Wrap mode ---

void TestTextViewWindowController::testBuildConfig_wrapModeNoWrap() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setWrapMode(TextEditorConfig::WrapMode::NoWrap);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(static_cast<int>(result->m_wrapMode), static_cast<int>(vte::WrapMode::NoWrap));
}

void TestTextViewWindowController::testBuildConfig_wrapModeWordWrap() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setWrapMode(TextEditorConfig::WrapMode::WordWrap);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(static_cast<int>(result->m_wrapMode), static_cast<int>(vte::WrapMode::WordWrap));
}

void TestTextViewWindowController::testBuildConfig_wrapModeWrapAnywhere() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setWrapMode(TextEditorConfig::WrapMode::WrapAnywhere);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(static_cast<int>(result->m_wrapMode), static_cast<int>(vte::WrapMode::WrapAnywhere));
}

void TestTextViewWindowController::testBuildConfig_wrapModeWordWrapOrAnywhere() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setWrapMode(TextEditorConfig::WrapMode::WordWrapOrAnywhere);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(static_cast<int>(result->m_wrapMode),
           static_cast<int>(vte::WrapMode::WordWrapOrAnywhere));
}

// --- Center cursor ---

void TestTextViewWindowController::testBuildConfig_centerCursorNever() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setCenterCursor(TextEditorConfig::CenterCursor::NeverCenter);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(static_cast<int>(result->m_centerCursor),
           static_cast<int>(vte::CenterCursor::NeverCenter));
}

void TestTextViewWindowController::testBuildConfig_centerCursorAlways() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setCenterCursor(TextEditorConfig::CenterCursor::AlwaysCenter);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(static_cast<int>(result->m_centerCursor),
           static_cast<int>(vte::CenterCursor::AlwaysCenter));
}

void TestTextViewWindowController::testBuildConfig_centerCursorOnBottom() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setCenterCursor(TextEditorConfig::CenterCursor::CenterOnBottom);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(static_cast<int>(result->m_centerCursor),
           static_cast<int>(vte::CenterCursor::CenterOnBottom));
}

// --- Line ending policy ---

void TestTextViewWindowController::testBuildConfig_lineEndingPlatform() {
  auto ec = makeEditorConfig();
  ec.setLineEndingPolicy(LineEndingPolicy::Platform);
  auto &tc = ec.getTextEditorConfig();

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_lineEndingPolicy, vte::LineEndingPolicy::Platform);
}

void TestTextViewWindowController::testBuildConfig_lineEndingFile() {
  auto ec = makeEditorConfig();
  ec.setLineEndingPolicy(LineEndingPolicy::File);
  auto &tc = ec.getTextEditorConfig();

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_lineEndingPolicy, vte::LineEndingPolicy::File);
}

void TestTextViewWindowController::testBuildConfig_lineEndingLF() {
  auto ec = makeEditorConfig();
  ec.setLineEndingPolicy(LineEndingPolicy::LF);
  auto &tc = ec.getTextEditorConfig();

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_lineEndingPolicy, vte::LineEndingPolicy::LF);
}

void TestTextViewWindowController::testBuildConfig_lineEndingCRLF() {
  auto ec = makeEditorConfig();
  ec.setLineEndingPolicy(LineEndingPolicy::CRLF);
  auto &tc = ec.getTextEditorConfig();

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_lineEndingPolicy, vte::LineEndingPolicy::CRLF);
}

void TestTextViewWindowController::testBuildConfig_lineEndingCR() {
  auto ec = makeEditorConfig();
  ec.setLineEndingPolicy(LineEndingPolicy::CR);
  auto &tc = ec.getTextEditorConfig();

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_lineEndingPolicy, vte::LineEndingPolicy::CR);
}

// --- Boolean fields ---

void TestTextViewWindowController::testBuildConfig_expandTab() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();

  tc.setExpandTabEnabled(true);
  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);
  QCOMPARE(result->m_expandTab, true);

  tc.setExpandTabEnabled(false);
  result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);
  QCOMPARE(result->m_expandTab, false);
}

void TestTextViewWindowController::testBuildConfig_textFolding() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();

  tc.setTextFoldingEnabled(true);
  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);
  QCOMPARE(result->m_textFoldingEnabled, true);

  tc.setTextFoldingEnabled(false);
  result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);
  QCOMPARE(result->m_textFoldingEnabled, false);
}

void TestTextViewWindowController::testBuildConfig_highlightWhitespace() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();

  tc.setHighlightWhitespaceEnabled(true);
  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);
  QCOMPARE(result->m_highlightWhitespace, true);

  tc.setHighlightWhitespaceEnabled(false);
  result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);
  QCOMPARE(result->m_highlightWhitespace, false);
}

// --- Integer fields ---

void TestTextViewWindowController::testBuildConfig_tabStopWidth() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  tc.setTabStopWidth(8);

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_tabStopWidth, 8);
}

// --- Scale factor ---

void TestTextViewWindowController::testBuildConfig_scaleFactor() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 2.5);

  QCOMPARE(result->m_scaleFactor, 2.5);
}

// --- Theme strings ---

void TestTextViewWindowController::testBuildConfig_emptyThemeFile() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  // Empty theme file means no theme is loaded.
  QVERIFY(result->m_theme.isNull());
}

void TestTextViewWindowController::testBuildConfig_syntaxTheme() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();
  QString syntaxTheme = QStringLiteral("Solarized Dark");

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), syntaxTheme, 1.0);

  QCOMPARE(result->m_syntaxTheme, syntaxTheme);
}

// --- ViConfig forwarding ---

void TestTextViewWindowController::testBuildConfig_viConfigForwarded() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();

  const auto &viConfig = ec.getViConfig();
  QVERIFY(!viConfig.isNull());

  auto result = TextViewWindowController::buildTextEditorConfig(
      ec, tc, QString(), QString(), 1.0);

  QCOMPARE(result->m_viConfig.data(), viConfig.data());
}

// ============ Group 2: buildTextEditorParameters ============

void TestTextViewWindowController::testBuildParams_spellCheckEnabled() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();

  tc.setSpellCheckEnabled(true);
  auto result = TextViewWindowController::buildTextEditorParameters(ec, tc);
  QCOMPARE(result->m_spellCheckEnabled, true);

  tc.setSpellCheckEnabled(false);
  result = TextViewWindowController::buildTextEditorParameters(ec, tc);
  QCOMPARE(result->m_spellCheckEnabled, false);
}

void TestTextViewWindowController::testBuildParams_autoDetectLanguage() {
  // EditorConfig's default for autoDetect is false (set in initDefaults).
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();

  auto result = TextViewWindowController::buildTextEditorParameters(ec, tc);
  QCOMPARE(result->m_autoDetectLanguageEnabled, false);
}

void TestTextViewWindowController::testBuildParams_defaultSpellCheckLanguage() {
  auto ec = makeEditorConfig();
  auto &tc = ec.getTextEditorConfig();

  // Default dictionary is "en_US" after EditorConfig::initDefaults().
  auto result = TextViewWindowController::buildTextEditorParameters(ec, tc);
  QCOMPARE(result->m_defaultSpellCheckLanguage, QStringLiteral("en_US"));

  // Set a custom dictionary.
  ec.setSpellCheckDefaultDictionary(QStringLiteral("de_DE"));
  result = TextViewWindowController::buildTextEditorParameters(ec, tc);
  QCOMPARE(result->m_defaultSpellCheckLanguage, QStringLiteral("de_DE"));
}

// ============ Group 3: prepareBufferState ============

void TestTextViewWindowController::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, this);

  QString nbPath = m_tempDir.filePath(QStringLiteral("ctrl_test"));
  QString configJson =
      QStringLiteral(R"({"name": "Ctrl Test", "description": "Test", "version": "1"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());

  // Create test files with different extensions.
  QString mdId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("note.md"));
  QVERIFY(!mdId.isEmpty());

  QString txtId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("readme.txt"));
  QVERIFY(!txtId.isEmpty());
}

void TestTextViewWindowController::cleanupTestCase() {
  delete m_bufferService;
  m_bufferService = nullptr;

  delete m_hookMgr;
  m_hookMgr = nullptr;

  delete m_notebookService;
  m_notebookService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestTextViewWindowController::cleanup() {
  // Close all open buffers between tests.
  QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufVal : buffers) {
    QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

void TestTextViewWindowController::testPrepareBufferState_invalidBuffer() {
  Buffer2 buf; // Default-constructed, invalid.
  auto state = TextViewWindowController::prepareBufferState(buf);

  QCOMPARE(state.valid, false);
  QCOMPARE(state.revision, 0);
}

void TestTextViewWindowController::testPrepareBufferState_validBuffer() {
  Buffer2 buf = m_bufferService->openBuffer(
      NodeIdentifier{m_notebookId, QStringLiteral("note.md")});
  QVERIFY(buf.isValid());

  auto state = TextViewWindowController::prepareBufferState(buf);

  QCOMPARE(state.valid, true);
  QCOMPARE(state.readOnly, false);
  QCOMPARE(state.syntaxSuffix, QStringLiteral("md"));
  // Content should match what peekContentRaw returns (as UTF-8 -> QString).
  QCOMPARE(state.content, QString::fromUtf8(buf.peekContentRaw()));
}

void TestTextViewWindowController::testPrepareBufferState_modifiedBuffer() {
  Buffer2 buf = m_bufferService->openBuffer(
      NodeIdentifier{m_notebookId, QStringLiteral("note.md")});
  QVERIFY(buf.isValid());

  QVERIFY(buf.setContentRaw(QByteArray("modified content")));

  auto state = TextViewWindowController::prepareBufferState(buf);

  QCOMPARE(state.valid, true);
  QCOMPARE(state.modified, true);
  QCOMPARE(state.content, QStringLiteral("modified content"));
}

void TestTextViewWindowController::testPrepareBufferState_txtExtension() {
  Buffer2 buf = m_bufferService->openBuffer(
      NodeIdentifier{m_notebookId, QStringLiteral("readme.txt")});
  QVERIFY(buf.isValid());

  auto state = TextViewWindowController::prepareBufferState(buf);

  QCOMPARE(state.valid, true);
  QCOMPARE(state.syntaxSuffix, QStringLiteral("txt"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTextViewWindowController)
#include "test_textviewwindowcontroller.moc"
