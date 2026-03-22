#include <QtTest>

#include <vtextedit/texteditorconfig.h>

#include <controllers/markdowneditorcontroller.h>
#include <core/editorconfig.h>
#include <core/global.h>
#include <core/markdowneditorconfig.h>
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

class TestMarkdownEditorController : public QObject {
  Q_OBJECT

private slots:
  // ============ Group 1: shouldEnableSectionNumber (static) ============

  void testSectionNumber_noneModeRead();
  void testSectionNumber_noneModeEdit();
  void testSectionNumber_readModeRead();
  void testSectionNumber_readModeEdit();
  void testSectionNumber_editModeEdit();
  void testSectionNumber_editModeRead();

  // ============ Group 2: getPreviewHelperConfig (static) ============

  void testPreviewHelper_allDefaults();
  void testPreviewHelper_webPlantUmlDisabled();
  void testPreviewHelper_webGraphvizDisabled();
  void testPreviewHelper_inplaceCodeBlock();
  void testPreviewHelper_inplaceMath();
  void testPreviewHelper_noInplacePreview();
  void testPreviewHelper_onlyImageLink();

  // ============ Group 3: buildMarkdownEditorParameters (static) ============

  void testBuildParams_spellCheckEnabled();
  void testBuildParams_spellCheckDisabled();
  void testBuildParams_autoDetectLanguage();
  void testBuildParams_defaultDictionary();
  void testBuildParams_customDictionary();

  // ============ Group 4: prepareBufferState (static, requires vxcore) ============

  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testPrepareBufferState_invalidBuffer();
  void testPrepareBufferState_validBuffer();
  void testPrepareBufferState_modifiedBuffer();

private:
  EditorConfig makeEditorConfig() { return EditorConfig(nullptr, nullptr); }

  static const int c_readMode = static_cast<int>(ViewWindowMode::Read);
  static const int c_editMode = static_cast<int>(ViewWindowMode::Edit);

  // Group 4 members
  VxCoreContextHandle m_context = nullptr;
  BufferService *m_bufferService = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

// ============ Group 1: shouldEnableSectionNumber ============

void TestMarkdownEditorController::testSectionNumber_noneModeRead() {
  QCOMPARE(MarkdownEditorController::shouldEnableSectionNumber(
               MarkdownEditorConfig::SectionNumberMode::None, c_readMode),
           false);
}

void TestMarkdownEditorController::testSectionNumber_noneModeEdit() {
  QCOMPARE(MarkdownEditorController::shouldEnableSectionNumber(
               MarkdownEditorConfig::SectionNumberMode::None, c_editMode),
           false);
}

void TestMarkdownEditorController::testSectionNumber_readModeRead() {
  QCOMPARE(MarkdownEditorController::shouldEnableSectionNumber(
               MarkdownEditorConfig::SectionNumberMode::Read, c_readMode),
           true);
}

void TestMarkdownEditorController::testSectionNumber_readModeEdit() {
  QCOMPARE(MarkdownEditorController::shouldEnableSectionNumber(
               MarkdownEditorConfig::SectionNumberMode::Read, c_editMode),
           false);
}

void TestMarkdownEditorController::testSectionNumber_editModeEdit() {
  QCOMPARE(MarkdownEditorController::shouldEnableSectionNumber(
               MarkdownEditorConfig::SectionNumberMode::Edit, c_editMode),
           true);
}

void TestMarkdownEditorController::testSectionNumber_editModeRead() {
  QCOMPARE(MarkdownEditorController::shouldEnableSectionNumber(
               MarkdownEditorConfig::SectionNumberMode::Edit, c_readMode),
           false);
}

// ============ Group 2: getPreviewHelperConfig ============

void TestMarkdownEditorController::testPreviewHelper_allDefaults() {
  // MarkdownEditorConfig default: webPlantUml=true, webGraphviz=true,
  // inplacePreviewSources = ImageLink|CodeBlock|Math.
  MarkdownEditorConfig mdConfig(nullptr, nullptr, QSharedPointer<TextEditorConfig>());

  auto config = MarkdownEditorController::getPreviewHelperConfig(mdConfig);

  QCOMPARE(config.webPlantUmlEnabled, true);
  QCOMPARE(config.webGraphvizEnabled, true);
  QCOMPARE(config.inplacePreviewCodeBlocksEnabled, true);
  QCOMPARE(config.inplacePreviewMathBlocksEnabled, true);
}

void TestMarkdownEditorController::testPreviewHelper_webPlantUmlDisabled() {
  MarkdownEditorConfig mdConfig(nullptr, nullptr, QSharedPointer<TextEditorConfig>());
  mdConfig.setWebPlantUml(false);

  auto config = MarkdownEditorController::getPreviewHelperConfig(mdConfig);
  QCOMPARE(config.webPlantUmlEnabled, false);
  QCOMPARE(config.webGraphvizEnabled, true);
}

void TestMarkdownEditorController::testPreviewHelper_webGraphvizDisabled() {
  MarkdownEditorConfig mdConfig(nullptr, nullptr, QSharedPointer<TextEditorConfig>());
  mdConfig.setWebGraphviz(false);

  auto config = MarkdownEditorController::getPreviewHelperConfig(mdConfig);
  QCOMPARE(config.webPlantUmlEnabled, true);
  QCOMPARE(config.webGraphvizEnabled, false);
}

void TestMarkdownEditorController::testPreviewHelper_inplaceCodeBlock() {
  MarkdownEditorConfig mdConfig(nullptr, nullptr, QSharedPointer<TextEditorConfig>());
  // Only CodeBlock enabled.
  mdConfig.setInplacePreviewSources(MarkdownEditorConfig::InplacePreviewSource::CodeBlock);

  auto config = MarkdownEditorController::getPreviewHelperConfig(mdConfig);
  QCOMPARE(config.inplacePreviewCodeBlocksEnabled, true);
  QCOMPARE(config.inplacePreviewMathBlocksEnabled, false);
}

void TestMarkdownEditorController::testPreviewHelper_inplaceMath() {
  MarkdownEditorConfig mdConfig(nullptr, nullptr, QSharedPointer<TextEditorConfig>());
  // Only Math enabled.
  mdConfig.setInplacePreviewSources(MarkdownEditorConfig::InplacePreviewSource::Math);

  auto config = MarkdownEditorController::getPreviewHelperConfig(mdConfig);
  QCOMPARE(config.inplacePreviewCodeBlocksEnabled, false);
  QCOMPARE(config.inplacePreviewMathBlocksEnabled, true);
}

void TestMarkdownEditorController::testPreviewHelper_noInplacePreview() {
  MarkdownEditorConfig mdConfig(nullptr, nullptr, QSharedPointer<TextEditorConfig>());
  mdConfig.setInplacePreviewSources(MarkdownEditorConfig::InplacePreviewSource::NoInplacePreview);

  auto config = MarkdownEditorController::getPreviewHelperConfig(mdConfig);
  QCOMPARE(config.inplacePreviewCodeBlocksEnabled, false);
  QCOMPARE(config.inplacePreviewMathBlocksEnabled, false);
}

void TestMarkdownEditorController::testPreviewHelper_onlyImageLink() {
  MarkdownEditorConfig mdConfig(nullptr, nullptr, QSharedPointer<TextEditorConfig>());
  mdConfig.setInplacePreviewSources(MarkdownEditorConfig::InplacePreviewSource::ImageLink);

  auto config = MarkdownEditorController::getPreviewHelperConfig(mdConfig);
  // ImageLink does not affect CodeBlock or Math flags.
  QCOMPARE(config.inplacePreviewCodeBlocksEnabled, false);
  QCOMPARE(config.inplacePreviewMathBlocksEnabled, false);
}

// ============ Group 3: buildMarkdownEditorParameters ============

void TestMarkdownEditorController::testBuildParams_spellCheckEnabled() {
  auto ec = makeEditorConfig();
  auto &mdConfig = ec.getMarkdownEditorConfig();

  mdConfig.setSpellCheckEnabled(true);
  auto result = MarkdownEditorController::buildMarkdownEditorParameters(ec, mdConfig);
  QCOMPARE(result->m_spellCheckEnabled, true);
}

void TestMarkdownEditorController::testBuildParams_spellCheckDisabled() {
  auto ec = makeEditorConfig();
  auto &mdConfig = ec.getMarkdownEditorConfig();

  mdConfig.setSpellCheckEnabled(false);
  auto result = MarkdownEditorController::buildMarkdownEditorParameters(ec, mdConfig);
  QCOMPARE(result->m_spellCheckEnabled, false);
}

void TestMarkdownEditorController::testBuildParams_autoDetectLanguage() {
  auto ec = makeEditorConfig();
  auto &mdConfig = ec.getMarkdownEditorConfig();

  // Default is false.
  auto result = MarkdownEditorController::buildMarkdownEditorParameters(ec, mdConfig);
  QCOMPARE(result->m_autoDetectLanguageEnabled, false);
}

void TestMarkdownEditorController::testBuildParams_defaultDictionary() {
  auto ec = makeEditorConfig();
  auto &mdConfig = ec.getMarkdownEditorConfig();

  // Default dictionary is "en_US" after EditorConfig::initDefaults().
  auto result = MarkdownEditorController::buildMarkdownEditorParameters(ec, mdConfig);
  QCOMPARE(result->m_defaultSpellCheckLanguage, QStringLiteral("en_US"));
}

void TestMarkdownEditorController::testBuildParams_customDictionary() {
  auto ec = makeEditorConfig();
  auto &mdConfig = ec.getMarkdownEditorConfig();

  ec.setSpellCheckDefaultDictionary(QStringLiteral("de_DE"));
  auto result = MarkdownEditorController::buildMarkdownEditorParameters(ec, mdConfig);
  QCOMPARE(result->m_defaultSpellCheckLanguage, QStringLiteral("de_DE"));
}

// ============ Group 4: prepareBufferState ============

void TestMarkdownEditorController::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::AutoSave, this);

  QString nbPath = m_tempDir.filePath(QStringLiteral("md_ctrl_test"));
  QString configJson =
      QStringLiteral(R"({"name": "MdCtrl Test", "description": "Test", "version": "1"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());

  // Create test markdown file.
  QString mdId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test.md"));
  QVERIFY(!mdId.isEmpty());
}

void TestMarkdownEditorController::cleanupTestCase() {
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

void TestMarkdownEditorController::cleanup() {
  // Close all open buffers between tests.
  QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufVal : buffers) {
    QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

void TestMarkdownEditorController::testPrepareBufferState_invalidBuffer() {
  Buffer2 buf; // Default-constructed, invalid.
  auto state = MarkdownEditorController::prepareBufferState(buf);

  QCOMPARE(state.valid, false);
  QCOMPARE(state.revision, 0);
}

void TestMarkdownEditorController::testPrepareBufferState_validBuffer() {
  Buffer2 buf = m_bufferService->openBuffer(
      NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  auto state = MarkdownEditorController::prepareBufferState(buf);

  QCOMPARE(state.valid, true);
  QCOMPARE(state.readOnly, false);
  // Content should match what peekContentRaw returns.
  QCOMPARE(state.content, QString::fromUtf8(buf.peekContentRaw()));
  // basePath should not be empty for a valid buffer.
  QVERIFY(!state.basePath.isEmpty());
}

void TestMarkdownEditorController::testPrepareBufferState_modifiedBuffer() {
  Buffer2 buf = m_bufferService->openBuffer(
      NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QVERIFY(buf.setContentRaw(QByteArray("# Modified Markdown\n\nHello world.")));

  auto state = MarkdownEditorController::prepareBufferState(buf);

  QCOMPARE(state.valid, true);
  QCOMPARE(state.modified, true);
  QCOMPARE(state.content, QStringLiteral("# Modified Markdown\n\nHello world."));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMarkdownEditorController)
#include "test_markdowneditorcontroller.moc"
