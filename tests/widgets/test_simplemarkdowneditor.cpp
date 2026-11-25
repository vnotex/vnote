#include <QtTest>

#include <QAbstractTextDocumentLayout>
#include <QFileInfo>
#include <QPalette>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>
#include <QVBoxLayout>

#include <memory>
#include <vxcore/vxcore.h>

#include <vtextedit/vtextedit.h>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <gui/services/themeservice.h>
#include <widgets/editors/simplemarkdowneditor.h>

using namespace vnotex;

namespace {
qreal rangeWidth(SimpleMarkdownEditor &p_editor, int p_start, int p_end) {
  auto *doc = p_editor.document();
  const auto block = doc->findBlock(p_start);
  doc->documentLayout()->blockBoundingRect(block);
  const auto line = block.layout()->lineForTextPosition(p_start - block.position());
  if (!line.isValid()) {
    return -1;
  }
  return line.cursorToX(p_end - block.position()) - line.cursorToX(p_start - block.position());
}
} // namespace

class TestSimpleMarkdownEditor : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    m_themeConfig.themeName = QStringLiteral("pure");
    m_themeConfig.locale = QStringLiteral("en_US");
    const auto themesDir = QFINDTESTDATA("../../src/data/extra/themes");
    QVERIFY(!themesDir.isEmpty());
    m_themeConfig.appDataPath = QFileInfo(themesDir).absolutePath();
    m_palette = qApp->palette();
    m_directory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_directory->isValid());
    for (const char *name : {"TMP", "TEMP", "TMPDIR"}) {
      m_environment.append({QByteArray(name), qgetenv(name)});
      QVERIFY(qputenv(name, QFile::encodeName(m_directory->path())));
    }
    m_searchPaths = QDir::searchPaths(QStringLiteral("app"));
    vxcore_set_test_mode(1);
    QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
    m_configCore = std::make_unique<ConfigCoreService>(m_context);
    m_config = std::make_unique<ConfigMgr2>(m_configCore.get());
    m_config->init();
    m_editorConfig = m_config->getEditorConfig().toJson();
  }

  void cleanupTestCase() {
    m_config.reset();
    m_configCore.reset();
    if (m_context) {
      vxcore_context_destroy(m_context);
    }
    QDir::setSearchPaths(QStringLiteral("app"), m_searchPaths);
    for (const auto &entry : m_environment) {
      if (entry.second.isNull()) {
        qunsetenv(entry.first.constData());
      } else {
        qputenv(entry.first.constData(), entry.second);
      }
    }
    m_directory.reset();
  }

  void cleanup() {
    m_config->getEditorConfig().fromJson(m_editorConfig);
    qApp->setPalette(m_palette);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->unsetColorScheme();
#endif
  }

  void concealmentPreservesSource() {
    ThemeService theme(m_themeConfig);
    ServiceLocator services;
    services.registerService(&theme);
    services.registerService(m_config.get());
    SimpleMarkdownEditor editor(services);
    const auto destination = QStringLiteral("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
    const auto source = QStringLiteral("# Heading\n\n**bold** and `code`\n\n<b>literal</b>\n\n"
                                       "[label](%1)\n\n![image](%1)\n\n[ref]: %1\n\n%1\n\noutside")
                            .arg(destination);
    editor.setPlainText(source);
    editor.setReadOnly(true);
    auto cursor = editor.getTextEdit()->textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.getTextEdit()->setTextCursor(cursor);
    editor.resize(640, 360);
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));

    const int bold = source.indexOf(QStringLiteral("**bold**"));
    const int code = source.indexOf(QStringLiteral("`code`"));
    QTRY_VERIFY(qAbs(rangeWidth(editor, 0, 2)) < 0.1);
    QTRY_VERIFY(qAbs(rangeWidth(editor, bold, bold + 2)) < 0.1);
    QTRY_VERIFY(qAbs(rangeWidth(editor, bold + 6, bold + 8)) < 0.1);
    QTRY_VERIFY(qAbs(rangeWidth(editor, code, code + 1)) < 0.1);
    QVERIFY(rangeWidth(editor, bold + 2, bold + 6) > 1);
    const int link = source.indexOf(destination);
    const int image = source.indexOf(destination, link + destination.size());
    const int reference = source.indexOf(destination, image + destination.size());
    const int raw = source.lastIndexOf(destination);
    QTRY_VERIFY(qAbs(rangeWidth(editor, link, link + destination.size())) < 0.1);
    for (const int start : {image, reference}) {
      QTRY_VERIFY(rangeWidth(editor, start, start + destination.size()) > 1);
      QTRY_VERIFY(rangeWidth(editor, start, start + destination.size()) + 1 <
                  rangeWidth(editor, raw, raw + destination.size()));
    }

    QTest::keyClicks(editor.getTextEdit(), "ignored");
    QCOMPARE(editor.toPlainText(), source);
    QVERIFY(!editor.isModified());
    QVERIFY(!editor.document()->isUndoAvailable());
  }

  void themeChangesPreserveEditingState_data() {
    QTest::addColumn<bool>("readOnly");
    QTest::newRow("editable") << false;
    QTest::newRow("read-only") << true;
  }

  void themeChangesPreserveEditingState() {
    QFETCH(bool, readOnly);
    ThemeService theme(m_themeConfig);
    ServiceLocator services;
    services.registerService(&theme);
    services.registerService(m_config.get());
    SimpleMarkdownEditor editor(services);
    const auto source = QStringLiteral("# Heading\n\n**bold**\n\nbody");
    editor.setText(source);
    editor.resize(640, 360);
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));

    auto *edit = editor.getTextEdit();
    auto *doc = editor.document();
    auto cursor = edit->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    cursor.insertText(QStringLiteral(" retained"));
    cursor.endEditBlock();
    cursor.beginEditBlock();
    cursor.insertText(QStringLiteral(" undone"));
    cursor.endEditBlock();
    edit->undo();
    cursor = edit->textCursor();
    cursor.setPosition(source.size());
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    edit->setTextCursor(cursor);
    editor.setReadOnly(readOnly);
    QVERIFY(doc->isUndoAvailable());
    QVERIFY(doc->isRedoAvailable());
    const auto text = editor.toPlainText();
    const int undoSteps = doc->availableUndoSteps();
    const int redoSteps = doc->availableRedoSteps();
    const auto lightColor = edit->palette().color(QPalette::Text);
    QColor darkColor;
    enum class ThemeChange { Dark, Refresh, Light };
    for (const auto change : {ThemeChange::Dark, ThemeChange::Refresh, ThemeChange::Light}) {
      switch (change) {
      case ThemeChange::Dark:
        theme.switchTheme(QStringLiteral("everforest-dark"));
        QTRY_VERIFY(edit->palette().color(QPalette::Text) != lightColor);
        darkColor = edit->palette().color(QPalette::Text);
        break;
      case ThemeChange::Refresh:
        theme.refreshCurrentTheme();
        QCOMPARE(edit->palette().color(QPalette::Text), darkColor);
        break;
      case ThemeChange::Light:
        theme.switchTheme(QStringLiteral("pure"));
        QTRY_COMPARE(edit->palette().color(QPalette::Text), lightColor);
        break;
      }

      QCOMPARE(editor.toPlainText(), text);
      QVERIFY(editor.isModified());
      QCOMPARE(doc->availableUndoSteps(), undoSteps);
      QCOMPARE(doc->availableRedoSteps(), redoSteps);
      QCOMPARE(edit->textCursor().position(), cursor.position());
      QCOMPARE(edit->textCursor().anchor(), cursor.anchor());
      QCOMPARE(editor.isReadOnly(), readOnly);
      const int bold = source.indexOf(QStringLiteral("**bold**"));
      QTRY_VERIFY(qAbs(rangeWidth(editor, bold, bold + 2)) < 0.1);
    }

    editor.setReadOnly(false);
    edit->redo();
    QCOMPARE(editor.toPlainText(), text + QStringLiteral(" undone"));
    edit->undo();
    edit->undo();
    QCOMPARE(editor.toPlainText(), source);
    QVERIFY(!editor.isModified());
  }

  void fontSizeOverrideSurvivesThemeChanges() {
    ThemeService theme(m_themeConfig);
    ServiceLocator services;
    services.registerService(&theme);
    services.registerService(m_config.get());
    SimpleMarkdownEditor editor(services);
    SimpleMarkdownEditor peer(services);
    const auto source = QStringLiteral("# Heading\n\nbody");
    editor.setPlainText(source);
    peer.setPlainText(source);
    editor.resize(640, 360);
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));
    auto *edit = editor.getTextEdit();
    auto cursor = edit->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral(" retained"));
    cursor.setPosition(source.size());
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    edit->setTextCursor(cursor);
    const auto text = editor.toPlainText();
    const int undoSteps = editor.document()->availableUndoSteps();
    const auto headingSize = [](SimpleMarkdownEditor &p_editor) {
      for (const auto &range : p_editor.document()->firstBlock().layout()->formats()) {
        if (range.start <= 2 && range.start + range.length > 2 &&
            range.format.fontPointSize() > 0) {
          return range.format.fontPointSize();
        }
      }
      return qreal(0);
    };
    QTRY_VERIFY(headingSize(editor) > 0);
    const auto baseSize = editor.editorFontPointSize();
    const auto baseHeadingSize = headingSize(editor);
    editor.setFontPointSize(baseSize + 3);
    QTRY_COMPARE(edit->font().pointSize(), baseSize + 3);
    QTRY_COMPARE(headingSize(editor), baseHeadingSize + 3);
    QCOMPARE(peer.editorFontPointSize(), baseSize);
    editor.setFontPointSize(baseSize + 3);
    QCOMPARE(editor.editorFontPointSize(), baseSize + 3);
    editor.setFontPointSize(1);
    QCOMPARE(editor.editorFontPointSize(), 2);
    editor.setFontPointSize(0);
    QCOMPARE(editor.editorFontPointSize(), baseSize);
    QTRY_COMPARE(headingSize(editor), baseHeadingSize);

    // Setting the current size must still pin it when the next theme has a different base.
    editor.setFontPointSize(baseSize);
    editor.setReadOnly(true);
    for (const auto &name : {QStringLiteral("latex-light"), QStringLiteral("everforest-dark")}) {
      theme.switchTheme(name);
      theme.refreshCurrentTheme();
      QTRY_COMPARE(edit->font().pointSize(), baseSize);
      QTRY_VERIFY(headingSize(peer) > 0);
      QTRY_COMPARE(headingSize(editor), headingSize(peer) + baseSize - peer.editorFontPointSize());
      QCOMPARE(editor.toPlainText(), text);
      QCOMPARE(editor.document()->availableUndoSteps(), undoSteps);
      QCOMPARE(edit->textCursor().position(), cursor.position());
      QCOMPARE(edit->textCursor().anchor(), cursor.anchor());
      QVERIFY(editor.isReadOnly());
      QVERIFY(editor.isModified());
    }
    editor.setFontPointSize(0);
    QCOMPARE(editor.editorFontPointSize(), peer.editorFontPointSize());
    editor.setReadOnly(false);
    edit->undo();
    QCOMPARE(editor.toPlainText(), source);
    QVERIFY(!editor.isModified());
    edit->redo();
    QCOMPARE(editor.toPlainText(), text);
  }

  void formattingShortcutsFollowFocusAndReadOnly() {
    ThemeService theme(m_themeConfig);
    ServiceLocator services;
    services.registerService(&theme);
    services.registerService(m_config.get());
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *first = new SimpleMarkdownEditor(services, &host);
    auto *second = new SimpleMarkdownEditor(services, &host);
    layout->addWidget(first);
    layout->addWidget(second);
    first->setPlainText(QStringLiteral("first"));
    second->setPlainText(QStringLiteral("second"));
    host.resize(640, 360);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    host.activateWindow();
    QApplication::setActiveWindow(&host);
    first->getTextEdit()->setFocus();
    first->getTextEdit()->selectAll();
    QTRY_VERIFY(first->getTextEdit()->hasFocus());
    QTest::keySequence(first->getTextEdit(), QKeySequence(QStringLiteral("Ctrl+B")));
    QCOMPARE(first->toPlainText(), QStringLiteral("**first**"));
    QCOMPARE(second->toPlainText(), QStringLiteral("second"));
    QTest::keySequence(first->getTextEdit(), QKeySequence::Undo);
    QCOMPARE(first->toPlainText(), QStringLiteral("first"));

    second->getTextEdit()->setFocus();
    second->getTextEdit()->selectAll();
    QTRY_VERIFY(second->getTextEdit()->hasFocus());
    QTest::keySequence(second->getTextEdit(), QKeySequence(QStringLiteral("Ctrl+G, M")));
    QCOMPARE(second->toPlainText(), QStringLiteral("==second=="));
    QCOMPARE(first->toPlainText(), QStringLiteral("first"));
    second->setReadOnly(true);
    QTest::keySequence(second->getTextEdit(), QKeySequence(QStringLiteral("Ctrl+B")));
    QCOMPARE(second->toPlainText(), QStringLiteral("==second=="));
    QCOMPARE(first->toPlainText(), QStringLiteral("first"));
    second->getTextEdit()->setReadOnly(false);
    second->setPlainText(QStringLiteral("heading"));
    QTest::keySequence(second->getTextEdit(), QKeySequence(QStringLiteral("Ctrl+3")));
    QCOMPARE(second->toPlainText(), QStringLiteral("### heading"));
    QTest::keySequence(second->getTextEdit(), QKeySequence(QStringLiteral("Ctrl+7")));
    QCOMPARE(second->toPlainText(), QStringLiteral("heading"));
  }

  void formattingShortcutsRespectConfiguredBindings() {
    auto configured = m_editorConfig;
    auto core = configured.value(QStringLiteral("core")).toObject();
    auto shortcuts = core.value(QStringLiteral("shortcuts")).toObject();
    shortcuts.insert(QStringLiteral("TypeBold"), QStringLiteral("Ctrl+Alt+B"));
    shortcuts.insert(QStringLiteral("TypeItalic"), QString());
    core.insert(QStringLiteral("shortcuts"), shortcuts);
    configured.insert(QStringLiteral("core"), core);
    m_config->getEditorConfig().fromJson(configured);
    ThemeService theme(m_themeConfig);
    ServiceLocator services;
    services.registerService(&theme);
    services.registerService(m_config.get());
    SimpleMarkdownEditor editor(services);
    editor.setPlainText(QStringLiteral("custom"));
    editor.resize(640, 240);
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));
    editor.activateWindow();
    QApplication::setActiveWindow(&editor);
    editor.getTextEdit()->setFocus();
    editor.getTextEdit()->selectAll();
    QTRY_VERIFY(editor.getTextEdit()->hasFocus());
    QTest::keySequence(editor.getTextEdit(), QKeySequence(QStringLiteral("Ctrl+B")));
    QTest::keySequence(editor.getTextEdit(), QKeySequence(QStringLiteral("Ctrl+I")));
    QCOMPARE(editor.toPlainText(), QStringLiteral("custom"));
    QTest::keySequence(editor.getTextEdit(), QKeySequence(QStringLiteral("Ctrl+Alt+B")));
    QCOMPARE(editor.toPlainText(), QStringLiteral("**custom**"));
  }

private:
  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<QTemporaryDir> m_directory;
  QList<QPair<QByteArray, QByteArray>> m_environment;
  QStringList m_searchPaths;
  std::unique_ptr<ConfigCoreService> m_configCore;
  std::unique_ptr<ConfigMgr2> m_config;
  QJsonObject m_editorConfig;
  ThemeServiceConfig m_themeConfig;
  QPalette m_palette;
};

QTEST_MAIN(TestSimpleMarkdownEditor)
#include "test_simplemarkdowneditor.moc"
