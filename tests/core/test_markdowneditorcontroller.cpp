#include <QtTest>

#include <QBuffer>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTextCursor>
#include <QTextDocument>

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/markdownhighlighterdata.h>
#include <vtextedit/markdownutils.h>
#include <vtextedit/texteditorconfig.h>

#include <controllers/markdowneditorcontroller.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/nodeidentifier.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/texteditorconfig.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>
#include <widgets/dialogs/imageinsertdialog.h>

using namespace vnotex;

namespace {
const char *kValidMarkdownThemeJson = R"({
  "metadata": { "name": "TestMdTheme", "revision": 0, "type": "vtextedit" },
  "editor-styles": {
    "Text": { "text-color": "#222222", "background-color": "#ffffff" }
  }
})";
} // anonymous namespace

namespace tests {

class TestMarkdownEditorController : public QObject {
  Q_OBJECT

private slots:
  void testSourceSectionNumbers_data();
  void testSourceSectionNumbers();

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

  // ============ Group 3b: buildMarkdownEditorConfigFromContent (static) ============

  void testBuildMarkdownEditorConfigFromContent_validJson();
  void testBuildMarkdownEditorConfigFromContent_emptyContent();
  void testBuildMarkdownEditorConfigFromContent_tableSourceOnByDefault();
  void testBuildMarkdownEditorConfigFromContent_tableSourceOff();
  void testBuildMarkdownEditorConfig_autoFoldPreviewedBlocks();

  // ============ Group 4: prepareBufferState (static, requires vxcore) ============

  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testPrepareBufferState_invalidBuffer();
  void testPrepareBufferState_validBuffer();
  void testPrepareBufferState_modifiedBuffer();

  void base64ReferencePreservesPixelsAndUndo();
  void base64ReferenceAvoidsCaseInsensitiveLabels();
  void imageInsertionChoice_data();
  void imageInsertionChoice();
  void invalidBase64ImageLeavesDocumentUntouched();

private:
  EditorConfig makeEditorConfig() { return EditorConfig(nullptr, nullptr); }

  // Group 4 members
  VxCoreContextHandle m_context = nullptr;
  BufferService *m_bufferService = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestMarkdownEditorController::testSourceSectionNumbers_data() {
  QTest::addColumn<QVector<int>>("levels");
  QTest::addColumn<QString>("pattern");
  QTest::addColumn<QVector<QString>>("expected");
  QTest::newRow("title") << QVector<int>{1, 2, 3, 2} << QStringLiteral("1.1.")
                         << QVector<QString>{"", "1.", "1.1.", "2."};
  QTest::newRow("multiple-h1") << QVector<int>{1, 2, 1} << QStringLiteral("1.1.")
                               << QVector<QString>{"1.", "1.1.", "2."};
  QTest::newRow("nonfirst-h1") << QVector<int>{2, 1} << QStringLiteral("1.1.")
                               << QVector<QString>{"1.1.", "2."};
  QTest::newRow("base-h3") << QVector<int>{1, 3, 3} << QStringLiteral("1.1.")
                           << QVector<QString>{"", "1.", "2."};
  QTest::newRow("skipped-level") << QVector<int>{1, 2, 4, 2} << QStringLiteral("1.1.")
                                 << QVector<QString>{"", "1.", "1.1.1.", "2."};
  QTest::newRow("parenthesis") << QVector<int>{1, 2, 3, 2} << QStringLiteral("1.1)")
                               << QVector<QString>{"", "1)", "1.1)", "2)"};
  QTest::newRow("no-suffix") << QVector<int>{1, 2, 3, 2} << QStringLiteral("1.1")
                             << QVector<QString>{"", "1", "1.1", "2"};
  QTest::newRow("invalid-pattern")
      << QVector<int>{2, 3, 2} << QStringLiteral("invalid") << QVector<QString>{"1.", "1.1.", "2."};
  QTest::newRow("empty") << QVector<int>{} << QStringLiteral("1.1.") << QVector<QString>{};
  QTest::newRow("title-only") << QVector<int>{1} << QStringLiteral("1.1.") << QVector<QString>{""};
  QTest::newRow("invalid-levels") << QVector<int>{0, 1, -1, 2, 0, 3} << QStringLiteral("1.1.")
                                  << QVector<QString>{"", "", "", "1.", "", "1.1."};
}

void TestMarkdownEditorController::testSourceSectionNumbers() {
  QFETCH(QVector<int>, levels);
  QFETCH(QString, pattern);
  QFETCH(QVector<QString>, expected);
  QVector<vte::md::HeadingInfo> headings;
  for (int i = 0; i < levels.size(); ++i) {
    vte::md::HeadingInfo heading;
    heading.m_level = levels[i];
    heading.m_title = QStringLiteral("Topic %1").arg(i);
    headings.append(heading);
  }
  QCOMPARE(MarkdownEditorController::generateSectionNumbers(headings, pattern), expected);
  // Authored numbers, including out-of-order ones, never suppress maintenance.
  for (int i = 0; i < headings.size(); ++i) {
    headings[i].m_title.prepend(QStringLiteral("%1) ").arg(42 - i));
  }
  QCOMPARE(MarkdownEditorController::generateSectionNumbers(headings, pattern), expected);
}

// ============ Group 2: getPreviewHelperConfig ============

void TestMarkdownEditorController::testPreviewHelper_allDefaults() {
  // MarkdownEditorConfig default: webPlantUml=true, webGraphviz=true,
  // inplacePreviewSources = ImageLink|CodeBlock|Math|Table.
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

// ============ Group 3b: buildMarkdownEditorConfigFromContent ============

void TestMarkdownEditorController::testBuildMarkdownEditorConfigFromContent_validJson() {
  auto ec = makeEditorConfig();
  auto &mdConfig = ec.getMarkdownEditorConfig();

  auto config = MarkdownEditorController::buildMarkdownEditorConfigFromContent(
      ec, mdConfig, QString::fromUtf8(kValidMarkdownThemeJson), QStringLiteral("default"), 1.0, 0);

  QVERIFY(!config.isNull());
  QVERIFY2(!config->m_textEditorConfig->m_theme.isNull(),
           "expected theme to be constructed from content");
}

void TestMarkdownEditorController::testBuildMarkdownEditorConfigFromContent_emptyContent() {
  auto ec = makeEditorConfig();
  auto &mdConfig = ec.getMarkdownEditorConfig();

  auto config = MarkdownEditorController::buildMarkdownEditorConfigFromContent(
      ec, mdConfig, QString(), QStringLiteral("default"), 1.0, 0);

  QVERIFY(!config.isNull());
  QVERIFY2(config->m_textEditorConfig->m_theme.isNull(),
           "empty content should yield null theme but valid config");
}

void TestMarkdownEditorController::
    testBuildMarkdownEditorConfigFromContent_tableSourceOnByDefault() {
  auto ec = makeEditorConfig();
  auto &mdConfig = ec.getMarkdownEditorConfig();

  auto config = MarkdownEditorController::buildMarkdownEditorConfigFromContent(
      ec, mdConfig, QString(), QStringLiteral("default"), 1.0, 0);

  QVERIFY(!config.isNull());
  QVERIFY2(config->m_inplacePreviewSources & vte::MarkdownEditorConfig::Table,
           "the interactive table sheet must be on by default");
}

void TestMarkdownEditorController::testBuildMarkdownEditorConfigFromContent_tableSourceOff() {
  auto ec = makeEditorConfig();
  auto &mdConfig = ec.getMarkdownEditorConfig();
  mdConfig.setInplacePreviewSources(MarkdownEditorConfig::InplacePreviewSource::ImageLink);

  auto config = MarkdownEditorController::buildMarkdownEditorConfigFromContent(
      ec, mdConfig, QString(), QStringLiteral("default"), 1.0, 0);

  QVERIFY(!config.isNull());
  QVERIFY2(!(config->m_inplacePreviewSources & vte::MarkdownEditorConfig::Table),
           "the table source must follow the user setting");
}

void TestMarkdownEditorController::testBuildMarkdownEditorConfig_autoFoldPreviewedBlocks() {
  auto ec = makeEditorConfig();
  auto &mdConfig = ec.getMarkdownEditorConfig();

  // Both builders share applyMarkdownConfigFields(), but assert through both anyway: the
  // content-based one is the only builder with production callers.
  auto content = MarkdownEditorController::buildMarkdownEditorConfigFromContent(
      ec, mdConfig, QString(), QStringLiteral("default"), 1.0, 0);
  QVERIFY(!content.isNull());
  QVERIFY2(content->m_autoFoldPreviewedBlocksEnabled, "auto-folding must default to on");

  mdConfig.setAutoFoldPreviewedBlocksEnabled(false);
  content = MarkdownEditorController::buildMarkdownEditorConfigFromContent(
      ec, mdConfig, QString(), QStringLiteral("default"), 1.0, 0);
  QVERIFY(!content.isNull());
  QVERIFY2(!content->m_autoFoldPreviewedBlocksEnabled, "the flag must follow the user setting");

  QTemporaryDir themeDir;
  QVERIFY(themeDir.isValid());
  const QString themeFile = QDir(themeDir.path()).filePath(QStringLiteral("md.theme"));
  {
    QFile f(themeFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(kValidMarkdownThemeJson),
             static_cast<qint64>(qstrlen(kValidMarkdownThemeJson)));
  }

  auto fileBased = MarkdownEditorController::buildMarkdownEditorConfig(
      ec, mdConfig, themeFile, QStringLiteral("default"), 1.0, 0);
  QVERIFY(!fileBased.isNull());
  QVERIFY2(!fileBased->m_autoFoldPreviewedBlocksEnabled, "the flag must follow the user setting");

  mdConfig.setAutoFoldPreviewedBlocksEnabled(true);
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
  QString mdId = m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test.md"));
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
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
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
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(buf.isValid());

  QVERIFY(buf.setContentRaw(QByteArray("# Modified Markdown\n\nHello world.")));

  auto state = MarkdownEditorController::prepareBufferState(buf);

  QCOMPARE(state.valid, true);
  QCOMPARE(state.modified, true);
  QCOMPARE(state.content, QStringLiteral("# Modified Markdown\n\nHello world."));
}

void TestMarkdownEditorController::base64ReferencePreservesPixelsAndUndo() {
  QImage image(3, 2, QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  image.setPixelColor(0, 0, QColor(17, 91, 203, 255));
  image.setPixelColor(1, 0, QColor(241, 7, 63, 127));
  image.setPixelColor(2, 1, QColor(0, 255, 0, 255));
  ImageInsertDialog dialog(QStringLiteral("Image"), QStringLiteral("a [pixel]"),
                           QStringLiteral("caption \"quoted\""), QString(), nullptr, false);
  dialog.setImage(image);
  dialog.setImageSource(ImageInsertDialog::ImageData);
  dialog.setEncryptedNote(true);

  QTextDocument document;
  const QString original = QStringLiteral("before selection after");
  document.setPlainText(original);
  QTextCursor cursor(&document);
  cursor.setPosition(7);
  cursor.setPosition(16, QTextCursor::KeepAnchor);
  bool inserted = false;
  connect(&dialog, &QDialog::accepted, &document, [&]() {
    inserted = MarkdownEditorController::insertImageAsBase64(
        cursor, dialog.getImageTitle(), dialog.getImageAltText(), dialog.getImageData());
  });
  dialog.getDialogButtonBox()->button(QDialogButtonBox::Ok)->click();
  QVERIFY(inserted);
  const auto content = document.toPlainText();
  const auto images =
      vte::MarkdownUtils::fetchImageLinks(content, QString(), vte::MarkdownLink::TypeFlag::Remote);
  QCOMPARE(images.size(), 1);
  const auto &link = images.first();
  QVERIFY(!link.hasUrlSpan());
  QCOMPARE(link.m_alt, QStringLiteral("a [pixel]"));
  QCOMPARE(link.m_title, QStringLiteral("caption \"quoted\""));
  QVERIFY(link.m_urlInLink.startsWith(QStringLiteral("data:image/png;base64,")));
  const auto encoded = link.m_urlInLink.mid(link.m_urlInLink.indexOf(QLatin1Char(',')) + 1);
  const auto decoded =
      QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
  QVERIFY(decoded);
  QCOMPARE(QImage::fromData(decoded.decoded).convertToFormat(QImage::Format_ARGB32), image);
  QCOMPARE(cursor.position(), link.m_regionEnd);
  QVERIFY(!cursor.hasSelection());
  QCOMPARE(content.left(link.m_regionStart), QStringLiteral("before "));
  QVERIFY(content.mid(link.m_regionEnd).startsWith(QStringLiteral(" after")));
  document.undo();
  QCOMPARE(document.toPlainText(), original);
  QVERIFY(!document.isUndoAvailable());
  document.redo();
  QCOMPARE(document.toPlainText(), content);
}

void TestMarkdownEditorController::base64ReferenceAvoidsCaseInsensitiveLabels() {
  QImage image(1, 1, QImage::Format_RGB32);
  image.fill(Qt::red);
  QByteArray bytes;
  QBuffer output(&bytes);
  QVERIFY(output.open(QIODevice::WriteOnly));
  QVERIFY(image.save(&output, "PNG"));
  QTextDocument document;
  document.setPlainText(QStringLiteral("![existing][image-1] and [IMAGE-2]\n\n"
                                       "[  ImAgE-1  ]: https://example.org/existing.png\n"));
  QTextCursor cursor(&document);
  cursor.movePosition(QTextCursor::End);
  QVERIFY(MarkdownEditorController::insertImageAsBase64(cursor, QStringLiteral("first"), QString(),
                                                        bytes));
  QVERIFY(MarkdownEditorController::insertImageAsBase64(cursor, QStringLiteral("second"), QString(),
                                                        bytes));
  const auto images = vte::MarkdownUtils::fetchImageLinks(document.toPlainText(), QString(),
                                                          vte::MarkdownLink::TypeFlag::Remote);
  QCOMPARE(images.size(), 3);
  QSet<QString> references;
  for (const auto &link : images) {
    if (link.m_alt == QStringLiteral("existing")) {
      QCOMPARE(link.m_urlInLink, QStringLiteral("https://example.org/existing.png"));
    } else {
      QVERIFY(link.m_urlInLink.startsWith(QStringLiteral("data:image/png;base64,")));
      const auto source =
          document.toPlainText().mid(link.m_regionStart, link.m_regionEnd - link.m_regionStart);
      references.insert(source.mid(source.lastIndexOf(QLatin1Char('['))).toCaseFolded());
    }
  }
  QCOMPARE(references.size(), 2);
  QVERIFY(!references.contains(QStringLiteral("[image-1]")));
  QVERIFY(!references.contains(QStringLiteral("[image-2]")));
}

void TestMarkdownEditorController::imageInsertionChoice_data() {
  QTest::addColumn<bool>("encrypted");
  QTest::addColumn<QByteArray>("format");
  QTest::newRow("plaintext-png") << false << QByteArrayLiteral("PNG");
  QTest::newRow("encrypted-xpm") << true << QByteArrayLiteral("XPM");
}

void TestMarkdownEditorController::imageInsertionChoice() {
  QFETCH(bool, encrypted);
  QFETCH(QByteArray, format);
  QTemporaryDir sourceDir;
  QVERIFY(sourceDir.isValid());
  const auto imagePath =
      sourceDir.filePath(QStringLiteral("source.") + QString::fromLatin1(format).toLower());
  QImage image(2, 2, QImage::Format_RGB32);
  image.fill(Qt::blue);
  QVERIFY(image.save(imagePath, format.constData()));
  const auto originalFiles =
      QDir(sourceDir.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
  ImageInsertDialog dialog(QStringLiteral("Image"), QString(), QString(), QString(), nullptr);
  dialog.setEncryptedNote(encrypted);
  dialog.setImagePath(imagePath);
  QTRY_VERIFY(!dialog.getImage().isNull());
  auto *choice = dialog.findChild<QComboBox *>(QStringLiteral("imageInsertMode"));
  QVERIFY(choice);
  QCOMPARE(choice->itemData(0).toBool(), encrypted);
  QCOMPARE(choice->currentIndex(), 0);
  QCOMPARE(dialog.insertAsBase64(), encrypted);
  auto *width = dialog.findChild<QLineEdit *>(QStringLiteral("imageWidthEdit"));
  auto *height = dialog.findChild<QLineEdit *>(QStringLiteral("imageHeightEdit"));
  QVERIFY(width && height);
  width->setText(QStringLiteral("42"));
  height->setText(QStringLiteral("17"));
  QCOMPARE(width->isEnabled(), !encrypted);
  choice->setFocus();
  QTest::keyClick(choice, encrypted ? Qt::Key_Down : Qt::Key_Up);
  QVERIFY(!dialog.insertAsBase64());
  QVERIFY(width->isEnabled());
  QCOMPARE(dialog.getImageWidth(), 42);
  QCOMPARE(dialog.getImageHeight(), 17);
  QTest::keyClick(choice, encrypted ? Qt::Key_Up : Qt::Key_Down);
  QVERIFY(dialog.insertAsBase64());
  QVERIFY(!width->isEnabled());
  QTextDocument document;
  QTextCursor cursor(&document);
  bool inserted = false;
  connect(&dialog, &QDialog::accepted, &document, [&]() {
    inserted = MarkdownEditorController::insertImageAsBase64(
        cursor, dialog.getImageTitle(), dialog.getImageAltText(), dialog.getImageData());
  });
  dialog.getDialogButtonBox()->button(QDialogButtonBox::Ok)->click();
  QVERIFY(inserted);
  const auto links = vte::MarkdownUtils::fetchImageLinks(document.toPlainText(), QString(),
                                                         vte::MarkdownLink::TypeFlag::Remote);
  QCOMPARE(links.size(), 1);
  QVERIFY(links.first().m_urlInLink.startsWith(QStringLiteral("data:image/png;base64,")));
  const auto encoded =
      links.first().m_urlInLink.mid(links.first().m_urlInLink.indexOf(QLatin1Char(',')) + 1);
  QCOMPARE(QImage::fromData(QByteArray::fromBase64(encoded.toLatin1()))
               .convertToFormat(QImage::Format_RGB32),
           image);
  QCOMPARE(QDir(sourceDir.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
           originalFiles);
  QCOMPARE(QImage(imagePath).convertToFormat(QImage::Format_RGB32), image);
}

void TestMarkdownEditorController::invalidBase64ImageLeavesDocumentUntouched() {
  QTextDocument document;
  document.setPlainText(QStringLiteral("selected text"));
  QTextCursor cursor(&document);
  cursor.select(QTextCursor::Document);
  QVERIFY(!MarkdownEditorController::insertImageAsBase64(cursor, QString(), QString(),
                                                         QByteArrayLiteral("not an image")));
  QCOMPARE(document.toPlainText(), QStringLiteral("selected text"));
  QCOMPARE(cursor.selectedText(), QStringLiteral("selected text"));
  QVERIFY(!document.isUndoAvailable());
}

} // namespace tests

QTEST_MAIN(tests::TestMarkdownEditorController)
#include "test_markdowneditorcontroller.moc"
