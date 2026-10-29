#include <QtTest>

#include <QShortcut>
#include <QTextBlock>
#include <QTextLayout>
#include <QVBoxLayout>

#include <widgets/plaintextedit.h>

using namespace vnotex;

namespace {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
constexpr auto c_control = Qt::MetaModifier;
#else
constexpr auto c_control = Qt::ControlModifier;
#endif

void setCursor(QPlainTextEdit &p_edit, int p_anchor, int p_position) {
  auto cursor = p_edit.textCursor();
  cursor.setPosition(p_anchor);
  cursor.setPosition(p_position, QTextCursor::KeepAnchor);
  p_edit.setTextCursor(cursor);
}
} // namespace

class TestPlainTextEdit : public QObject {
  Q_OBJECT
private slots:
  void deletion_data();
  void deletion();
  void deletesLogicalLineAcrossWrap();
  void readOnlyPreservesText_data();
  void readOnlyPreservesText();
  void otherModifiersUseQtBehavior_data();
  void otherModifiersUseQtBehavior();
  void takesShortcutOnlyWhileEditable();
};

void TestPlainTextEdit::deletion_data() {
  QTest::addColumn<QString>("text");
  QTest::addColumn<int>("anchor");
  QTest::addColumn<int>("position");
  QTest::addColumn<int>("key");
  QTest::addColumn<QString>("expected");
  QTest::addColumn<int>("expectedPosition");

  QTest::newRow("word-before-caret") << QStringLiteral("alpha beta tail") << 10 << 10
                                     << int(Qt::Key_W) << QStringLiteral("alpha  tail") << 6;
  QTest::newRow("word-after-punctuation") << QStringLiteral("alpha.beta") << 10 << 10
                                          << int(Qt::Key_W) << QStringLiteral("alpha.") << 6;
  QTest::newRow("line-prefix-only") << QStringLiteral("first\n  alpha beta tail\nlast") << 18 << 18
                                    << int(Qt::Key_U) << QStringLiteral("first\n tail\nlast") << 6;
  QTest::newRow("line-start") << QStringLiteral("first\nsecond") << 6 << 6 << int(Qt::Key_U)
                              << QStringLiteral("first\nsecond") << 6;
  QTest::newRow("word-document-start")
      << QStringLiteral("alpha") << 0 << 0 << int(Qt::Key_W) << QStringLiteral("alpha") << 0;
  QTest::newRow("empty-document") << QStringLiteral("") << 0 << 0 << int(Qt::Key_U)
                                  << QStringLiteral("") << 0;
  QTest::newRow("partial-word-selection") << QStringLiteral("alpha beta") << 7 << 9
                                          << int(Qt::Key_W) << QStringLiteral("alpha ba") << 7;
  QTest::newRow("reverse-line-selection") << QStringLiteral("alpha beta") << 9 << 7
                                          << int(Qt::Key_U) << QStringLiteral("alpha ba") << 7;
  QTest::newRow("multiline-selection-word")
      << QStringLiteral("first\nsecond\nlast") << 3 << 9 << int(Qt::Key_W)
      << QStringLiteral("firond\nlast") << 3;
  QTest::newRow("multiline-selection-line")
      << QStringLiteral("first\nsecond\nlast") << 9 << 3 << int(Qt::Key_U)
      << QStringLiteral("firond\nlast") << 3;
}

void TestPlainTextEdit::deletion() {
  QFETCH(QString, text);
  QFETCH(int, anchor);
  QFETCH(int, position);
  QFETCH(int, key);
  QFETCH(QString, expected);
  QFETCH(int, expectedPosition);
  PlainTextEdit edit;
  edit.setPlainText(text);
  setCursor(edit, anchor, position);
  QTest::keyClick(&edit, Qt::Key(key), c_control);
  QCOMPARE(edit.toPlainText(), expected);
  QCOMPARE(edit.textCursor().position(), expectedPosition);
  QVERIFY(!edit.textCursor().hasSelection());
  QCOMPARE(edit.document()->isUndoAvailable(), text != expected);
  if (text != expected) {
    edit.undo();
    QCOMPARE(edit.toPlainText(), text);
    QVERIFY(!edit.document()->isUndoAvailable());
    edit.redo();
    QCOMPARE(edit.toPlainText(), expected);
  }
}

void TestPlainTextEdit::deletesLogicalLineAcrossWrap() {
  PlainTextEdit edit;
  const QString line = QStringLiteral("alpha beta gamma delta epsilon zeta eta theta");
  edit.resize(100, 250);
  edit.setPlainText(QStringLiteral("first\n") + line + QStringLiteral(" suffix\nlast"));
  edit.show();
  QVERIFY(QTest::qWaitForWindowExposed(&edit));
  setCursor(edit, 6 + line.size(), 6 + line.size());
  QVERIFY(edit.textCursor().block().layout()->lineCount() > 1);
  QTest::keyClick(&edit, Qt::Key_U, c_control);
  QCOMPARE(edit.toPlainText(), QStringLiteral("first\n suffix\nlast"));
  QCOMPARE(edit.textCursor().position(), 6);
}

void TestPlainTextEdit::readOnlyPreservesText_data() {
  QTest::addColumn<int>("key");
  QTest::addColumn<bool>("selected");
  QTest::newRow("word") << int(Qt::Key_W) << false;
  QTest::newRow("line") << int(Qt::Key_U) << false;
  QTest::newRow("word-selection") << int(Qt::Key_W) << true;
  QTest::newRow("line-selection") << int(Qt::Key_U) << true;
}

void TestPlainTextEdit::readOnlyPreservesText() {
  QFETCH(int, key);
  QFETCH(bool, selected);
  PlainTextEdit edit;
  const QString text = QStringLiteral("alpha beta\nlast");
  edit.setPlainText(text);
  setCursor(edit, selected ? 2 : 10, 10);
  edit.setReadOnly(true);
  QTest::keyClick(&edit, Qt::Key(key), c_control);
  QCOMPARE(edit.toPlainText(), text);
  QVERIFY(!edit.document()->isUndoAvailable());
}

void TestPlainTextEdit::otherModifiersUseQtBehavior_data() {
  QTest::addColumn<int>("key");
  QTest::addColumn<int>("modifiers");
  QTest::newRow("plain-w") << int(Qt::Key_W) << int(Qt::NoModifier);
  QTest::newRow("plain-u") << int(Qt::Key_U) << int(Qt::NoModifier);
  QTest::newRow("shift-control-w") << int(Qt::Key_W) << int(c_control | Qt::ShiftModifier);
  QTest::newRow("alt-control-u") << int(Qt::Key_U) << int(c_control | Qt::AltModifier);
  QTest::newRow("backspace") << int(Qt::Key_Backspace) << int(Qt::NoModifier);
  QTest::newRow("enter") << int(Qt::Key_Return) << int(Qt::NoModifier);
}

void TestPlainTextEdit::otherModifiersUseQtBehavior() {
  QFETCH(int, key);
  QFETCH(int, modifiers);
  PlainTextEdit edit;
  QPlainTextEdit baseline;
  for (QPlainTextEdit *widget : {static_cast<QPlainTextEdit *>(&edit), &baseline}) {
    widget->setPlainText(QStringLiteral("alpha beta"));
    setCursor(*widget, 8, 8);
    QTest::keyClick(widget, Qt::Key(key), Qt::KeyboardModifiers(modifiers));
  }
  QCOMPARE(edit.toPlainText(), baseline.toPlainText());
  QCOMPARE(edit.textCursor().position(), baseline.textCursor().position());
}

void TestPlainTextEdit::takesShortcutOnlyWhileEditable() {
  QWidget window;
  auto *layout = new QVBoxLayout(&window);
  auto *edit = new PlainTextEdit(&window);
  layout->addWidget(edit);
  QShortcut wordShortcut(QKeySequence(c_control | Qt::Key_W), &window);
  QShortcut lineShortcut(QKeySequence(c_control | Qt::Key_U), &window);
  QSignalSpy wordTriggered(&wordShortcut, &QShortcut::activated);
  QSignalSpy lineTriggered(&lineShortcut, &QShortcut::activated);
  edit->setPlainText(QStringLiteral("alpha beta"));
  setCursor(*edit, 10, 10);
  window.show();
  window.activateWindow();
  edit->setFocus();
  QVERIFY(QTest::qWaitForWindowActive(&window));
  QTest::keyClick(edit, Qt::Key_W, c_control);
  QCOMPARE(edit->toPlainText(), QStringLiteral("alpha "));
  QTest::keyClick(edit, Qt::Key_U, c_control);
  QCOMPARE(edit->toPlainText(), QString());
  QCOMPARE(wordTriggered.count(), 0);
  QCOMPARE(lineTriggered.count(), 0);

  edit->setReadOnly(true);
  QTest::keyClick(edit, Qt::Key_W, c_control);
  QTest::keyClick(edit, Qt::Key_U, c_control);
  QCOMPARE(wordTriggered.count(), 1);
  QCOMPARE(lineTriggered.count(), 1);
}

QTEST_MAIN(TestPlainTextEdit)
#include "test_plaintextedit.moc"
