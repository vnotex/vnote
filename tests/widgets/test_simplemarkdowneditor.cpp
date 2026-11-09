#include <QtTest>

#include <QAbstractTextDocumentLayout>
#include <QFileInfo>
#include <QPalette>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextLayout>

#include <vtextedit/vtextedit.h>

#include <core/servicelocator.h>
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
  }

  void cleanup() {
    qApp->setPalette(m_palette);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->unsetColorScheme();
#endif
  }

  void concealmentPreservesSource() {
    ThemeService theme(m_themeConfig);
    ServiceLocator services;
    services.registerService(&theme);
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

private:
  ThemeServiceConfig m_themeConfig;
  QPalette m_palette;
};

QTEST_MAIN(TestSimpleMarkdownEditor)
#include "test_simplemarkdowneditor.moc"
