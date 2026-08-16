// Real-editor coverage for the caret-preservation helpers used by
// ViewWindow2's reload path (src/gui/utils/textcursorpreserver.cpp).
//
// This exercises the PRODUCTION capture/restore code against a real QTextEdit —
// unlike the fakes in tests/widgets/test_viewsplit2_reload_menu.cpp, which only
// verify that ViewWindow2::reload() calls capture/restore in the right order.
//
// The editor is deliberately narrow so a long paragraph soft-wraps: that is the
// configuration in which QTextCursor::columnNumber() (visual-line relative)
// diverges from positionInBlock() (block relative). Using columnNumber() in the
// production helper makes testCaretBeyondFirstVisualLineSurvives fail.

#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QtTest>

#include <gui/utils/textcursorpreserver.h>
#include <utils/scrollpreservationpolicy.h>

using namespace vnotex;

namespace tests {

namespace {

// A single long paragraph that is guaranteed to wrap in a narrow viewport,
// followed by two short lines.
QString longWrappedDocument() {
  QString paragraph;
  for (int i = 0; i < 40; ++i) {
    paragraph += QStringLiteral("word%1 ").arg(i, 2, 10, QLatin1Char('0'));
  }
  paragraph.chop(1);
  return paragraph + QStringLiteral("\nsecond line\nthird line\n");
}

} // namespace

class TestTextCursorPreserver : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void testCaretBeyondFirstVisualLineSurvives();
  void testCaretClampedWhenDocumentShrinks();
  void testOffsetClampedToBlockText();
  void testInvalidPositionIsNoOp();
  void testRestoreDoesNotModifyDocument();
  void testScrollValueWinsOverEnsureVisible();

private:
  QTextEdit *m_edit = nullptr;
};

void TestTextCursorPreserver::init() {
  m_edit = new QTextEdit();
  m_edit->setLineWrapMode(QTextEdit::WidgetWidth);
  m_edit->resize(120, 80); // Narrow + short: forces soft wrapping and scrolling.
  m_edit->setPlainText(longWrappedDocument());
  m_edit->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_edit));
}

void TestTextCursorPreserver::cleanup() {
  delete m_edit;
  m_edit = nullptr;
}

void TestTextCursorPreserver::testCaretBeyondFirstVisualLineSurvives() {
  // Put the caret deep inside the first (wrapped) paragraph, well past the end
  // of its first visual line.
  QTextCursor cursor = m_edit->textCursor();
  cursor.setPosition(m_edit->document()->findBlockByNumber(0).position() + 120);
  m_edit->setTextCursor(cursor);
  QCOMPARE(m_edit->textCursor().blockNumber(), 0);
  QCOMPARE(m_edit->textCursor().positionInBlock(), 120);
  // Sanity: this is the property that makes columnNumber() the wrong capture.
  QVERIFY(m_edit->textCursor().columnNumber() < 120);

  const TextCursorPosition captured = captureTextCursorPosition(m_edit);
  QCOMPARE(captured.m_line, 0);
  QCOMPARE(captured.m_positionInBlock, 120);

  // Simulate the reload: the same content comes back via setPlainText(), which
  // resets the caret to the very beginning.
  m_edit->setPlainText(longWrappedDocument());
  QCOMPARE(m_edit->textCursor().position(), 0);

  restoreTextCursorPosition(m_edit, captured);

  QCOMPARE(m_edit->textCursor().blockNumber(), 0);
  QCOMPARE(m_edit->textCursor().positionInBlock(), 120);
}

void TestTextCursorPreserver::testCaretClampedWhenDocumentShrinks() {
  QTextCursor cursor = m_edit->textCursor();
  cursor.setPosition(m_edit->document()->findBlockByNumber(2).position() + 2);
  m_edit->setTextCursor(cursor);

  const TextCursorPosition captured = captureTextCursorPosition(m_edit);
  QCOMPARE(captured.m_line, 2);

  // The reloaded file is much shorter than the captured line index.
  m_edit->setPlainText(QStringLiteral("only line"));
  restoreTextCursorPosition(m_edit, captured);

  QCOMPARE(m_edit->document()->blockCount(), 1);
  QCOMPARE(m_edit->textCursor().blockNumber(), 0);
  QCOMPARE(m_edit->textCursor().positionInBlock(), 2);
}

void TestTextCursorPreserver::testOffsetClampedToBlockText() {
  TextCursorPosition captured;
  captured.m_line = 0;
  captured.m_positionInBlock = 999;

  m_edit->setPlainText(QStringLiteral("short\nlonger line here\n"));
  restoreTextCursorPosition(m_edit, captured);

  QCOMPARE(m_edit->textCursor().blockNumber(), 0);
  // Clamped to end-of-line, never spilling onto the next block.
  QCOMPARE(m_edit->textCursor().positionInBlock(), 5);
}

void TestTextCursorPreserver::testInvalidPositionIsNoOp() {
  m_edit->setPlainText(QStringLiteral("alpha\nbeta\n"));
  QTextCursor cursor = m_edit->textCursor();
  cursor.setPosition(3);
  m_edit->setTextCursor(cursor);

  restoreTextCursorPosition(m_edit, TextCursorPosition());
  QCOMPARE(m_edit->textCursor().position(), 3);

  restoreTextCursorPosition(nullptr, captureTextCursorPosition(m_edit));
  QCOMPARE(m_edit->textCursor().position(), 3);
}

void TestTextCursorPreserver::testRestoreDoesNotModifyDocument() {
  m_edit->setPlainText(longWrappedDocument());
  m_edit->document()->setModified(false);

  TextCursorPosition captured;
  captured.m_line = 1;
  captured.m_positionInBlock = 4;
  restoreTextCursorPosition(m_edit, captured);

  // setTextCursor() only touches cursor/selection state: the dirty flag that
  // ViewWindow2 derives its modified state from must stay clear.
  QVERIFY(!m_edit->document()->isModified());
  QCOMPARE(m_edit->textCursor().blockNumber(), 1);
}

void TestTextCursorPreserver::testScrollValueWinsOverEnsureVisible() {
  auto *vbar = m_edit->verticalScrollBar();
  QVERIFY(vbar->maximum() > 0);

  // Capture a caret near the end of the document plus a mid-document scroll
  // value, exactly as TextViewWindow2::capturePositionState() does.
  QTextCursor cursor = m_edit->textCursor();
  cursor.setPosition(m_edit->document()->findBlockByNumber(2).position());
  m_edit->setTextCursor(cursor);
  const TextCursorPosition captured = captureTextCursorPosition(m_edit);
  const int oldMax = vbar->maximum();
  const int oldValue = oldMax / 2;
  QVERIFY(oldValue > 0);
  vbar->setValue(oldValue);

  m_edit->setPlainText(longWrappedDocument());

  // Production restore order: cursor first (its implicit ensure-visible must
  // not survive), then the ScrollPreservationPolicy result.
  restoreTextCursorPosition(m_edit, captured);
  const int expected =
      ScrollPreservationPolicy::computeRestoredScrollValue(oldValue, oldMax, vbar->maximum());
  vbar->setValue(expected);

  QCOMPARE(vbar->value(), expected);
  QVERIFY(expected != vbar->maximum() || oldValue >= oldMax);
  QCOMPARE(m_edit->textCursor().blockNumber(), captured.m_line);
}

} // namespace tests

QTEST_MAIN(tests::TestTextCursorPreserver)
#include "test_textcursorpreserver.moc"
