#include <QLabel>
#include <QMenu>
#include <QTextCursor>
#include <QToolButton>
#include <QtTest>

#include <vtextedit/global.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/vtexteditor.h>

#include <widgets/editors/editorstatusbarbinder.h>
#include <widgets/editors/statusbar.h>

using namespace vnotex;

namespace tests {

// Verifies EditorStatusBarBinder wires a real vte::VTextEditor's signals to a
// column-based StatusBar: cursor/syntax label updates, spellcheck menu rebuild,
// and Vi input-mode widget mounting.
class TestEditorStatusBarBinder : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void testInitialSyncAndCursorUpdate();
  void testSyntaxUpdate();
  void testSpellCheckMenuRebuild();
  void testViWidgetMountOnInputModeChange();

private:
  // First column QLabel whose text contains p_needle (order-independent).
  static QLabel *labelContaining(StatusBar &p_bar, const QString &p_needle) {
    const auto labels = p_bar.findChildren<QLabel *>();
    for (auto *l : labels) {
      if (l->objectName() == QStringLiteral("StatusBarMessageLabel")) {
        continue;
      }
      if (l->text().contains(p_needle)) {
        return l;
      }
    }
    return nullptr;
  }
};

void TestEditorStatusBarBinder::initTestCase() {
  // Initialize the KSyntaxHighlighting repository so setSyntax() is safe.
  vte::VTextEditor::addSyntaxCustomSearchPaths(QStringList());
}

void TestEditorStatusBarBinder::testInitialSyncAndCursorUpdate() {
  vte::VTextEditor editor(nullptr, nullptr);
  EditorStatusBarBinder binder;
  auto def = binder.buildDef(&editor);
  StatusBar bar(def);
  binder.attach(&editor, &bar);

  // Initial cursor label shows the position.
  QVERIFY(labelContaining(bar, QStringLiteral("Line:")) != nullptr);

  editor.setText(QStringLiteral("aaa\nbbb\nccc"));
  auto *te = editor.getTextEdit();
  QTextCursor c = te->textCursor();
  c.movePosition(QTextCursor::End);
  te->setTextCursor(c);

  QVERIFY(labelContaining(bar, QStringLiteral("Line: 3")) != nullptr);
}

void TestEditorStatusBarBinder::testSyntaxUpdate() {
  vte::VTextEditor editor(nullptr, nullptr);
  EditorStatusBarBinder binder;
  auto def = binder.buildDef(&editor);
  StatusBar bar(def);
  binder.attach(&editor, &bar);

  editor.setSyntax(QStringLiteral("cpp"));
  // setSyntax normalizes unknown names to "plaintext"; the label must reflect
  // whatever getSyntax() resolves to, upper-cased.
  const QString expected = editor.getSyntax().toUpper();
  QVERIFY(!expected.isEmpty());
  QVERIFY(labelContaining(bar, expected) != nullptr);
}

void TestEditorStatusBarBinder::testSpellCheckMenuRebuild() {
  vte::VTextEditor editor(nullptr, nullptr);
  EditorStatusBarBinder binder;
  auto def = binder.buildDef(&editor);
  StatusBar bar(def);
  binder.attach(&editor, &bar);

  // The single QToolButton is the spellcheck menu.
  auto *btn = bar.findChild<QToolButton *>();
  QVERIFY(btn && btn->menu());

  editor.setSpellCheckEnabled(true);
  const auto acts = btn->menu()->actions();
  QVERIFY(acts.size() >= 2);
  // Item 0 = "Enable Spell Check", now checked.
  QVERIFY(acts.at(0)->isChecked());
}

void TestEditorStatusBarBinder::testViWidgetMountOnInputModeChange() {
  vte::VTextEditor editor(nullptr, nullptr);
  EditorStatusBarBinder binder;
  auto def = binder.buildDef(&editor);
  StatusBar bar(def);
  binder.attach(&editor, &bar);

  editor.setInputMode(vte::ViMode);

  auto viWidget = editor.inputModeStatusWidget();
  QVERIFY(!viWidget.isNull());
  QVERIFY(bar.isAncestorOf(viWidget.data()));

  // Leaving Vi mode detaches the widget from the bar.
  editor.setInputMode(vte::NormalMode);
  QVERIFY(!bar.isAncestorOf(viWidget.data()));
}

} // namespace tests

QTEST_MAIN(tests::TestEditorStatusBarBinder)
#include "test_editorstatusbarbinder.moc"
