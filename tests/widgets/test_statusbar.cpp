#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

#include <widgets/editors/statusbar.h>

using namespace vnotex;

namespace tests {

// Pure-view coverage for the column-based StatusBar infra (phase 1). Verifies
// column construction/order, hidden-when-empty, the granular setters, the
// bounds/type safety of those setters, and the interaction callbacks.
class TestStatusBar : public QObject {
  Q_OBJECT

private slots:
  void testBuildCreatesColumnsInOrder();
  void testEmptyDefHidesBar();
  void testSetColumnText();
  void testSetColumnVisible();
  void testSetColumnStyle();
  void testSetColumnIcon();
  void testSetColumnMenuActions();
  void testSetColumnEditText();
  void testOutOfRangeIsNoOp();
  void testWrongTypeIsNoOp();
  void testOnClickedFires();
  void testOnTriggeredFires();
  void testOnTextEditedFires();

private:
  // Find the Nth direct child widget of type T in layout order.
  template <typename T> static T *childAt(StatusBar &p_bar, int p_n) {
    const auto children = p_bar.findChildren<T *>(QString(), Qt::FindDirectChildrenOnly);
    return p_n >= 0 && p_n < children.size() ? children.at(p_n) : nullptr;
  }
};

void TestStatusBar::testBuildCreatesColumnsInOrder() {
  StatusBarDef def;
  StatusBarColumn label;
  label.type = StatusBarColumnType::Label;
  label.text = QStringLiteral("L");
  StatusBarColumn button;
  button.type = StatusBarColumnType::Button;
  button.text = QStringLiteral("B");
  StatusBarColumn spacer;
  spacer.type = StatusBarColumnType::Spacer;
  StatusBarColumn edit;
  edit.type = StatusBarColumnType::Edit;
  def << label << button << spacer << edit;

  StatusBar bar(def);
  // Label + Button + Edit widgets exist (Spacer adds no widget).
  QVERIFY(childAt<QLabel>(bar, 0) != nullptr);
  QCOMPARE(childAt<QLabel>(bar, 0)->text(), QStringLiteral("L"));
  QVERIFY(childAt<QToolButton>(bar, 0) != nullptr);
  QCOMPARE(childAt<QToolButton>(bar, 0)->text(), QStringLiteral("B"));
  QVERIFY(childAt<QLineEdit>(bar, 0) != nullptr);
}

void TestStatusBar::testEmptyDefHidesBar() {
  // An empty def yields a bar that stays hidden even inside a shown parent.
  QWidget parent;
  auto *bar = new StatusBar(StatusBarDef(), &parent);
  parent.show();
  QVERIFY(bar->isHidden());
}

void TestStatusBar::testSetColumnText() {
  StatusBarDef def;
  StatusBarColumn label;
  label.type = StatusBarColumnType::Label;
  def << label;
  StatusBar bar(def);

  bar.setColumnText(0, QStringLiteral("hello"));
  QCOMPARE(childAt<QLabel>(bar, 0)->text(), QStringLiteral("hello"));
}

void TestStatusBar::testSetColumnVisible() {
  StatusBarDef def;
  StatusBarColumn label;
  label.type = StatusBarColumnType::Label;
  def << label;
  StatusBar bar(def);
  bar.show();

  bar.setColumnVisible(0, false);
  QVERIFY(childAt<QLabel>(bar, 0)->isHidden());
  bar.setColumnVisible(0, true);
  QVERIFY(!childAt<QLabel>(bar, 0)->isHidden());
}

void TestStatusBar::testSetColumnStyle() {
  StatusBarDef def;
  StatusBarColumn label;
  label.type = StatusBarColumnType::Label;
  def << label;
  StatusBar bar(def);

  bar.setColumnStyle(0, QStringLiteral("color: red;"));
  QCOMPARE(childAt<QLabel>(bar, 0)->styleSheet(), QStringLiteral("color: red;"));
}

void TestStatusBar::testSetColumnIcon() {
  StatusBarDef def;
  StatusBarColumn button;
  button.type = StatusBarColumnType::Button;
  def << button;
  StatusBar bar(def);

  QPixmap pm(8, 8);
  pm.fill(Qt::red);
  bar.setColumnIcon(0, QIcon(pm));
  QVERIFY(!childAt<QToolButton>(bar, 0)->icon().isNull());
}

void TestStatusBar::testSetColumnMenuActions() {
  StatusBarDef def;
  StatusBarColumn menu;
  menu.type = StatusBarColumnType::Menu;
  menu.menuActions = QStringList{QStringLiteral("a"), QStringLiteral("b")};
  def << menu;
  StatusBar bar(def);

  QToolButton *button = childAt<QToolButton>(bar, 0);
  QVERIFY(button && button->menu());
  QCOMPARE(button->menu()->actions().size(), 2);

  bar.setColumnMenuActions(0, QStringList{QStringLiteral("x")});
  QCOMPARE(button->menu()->actions().size(), 1);
  QCOMPARE(button->menu()->actions().first()->text(), QStringLiteral("x"));
}

void TestStatusBar::testSetColumnEditText() {
  StatusBarDef def;
  StatusBarColumn edit;
  edit.type = StatusBarColumnType::Edit;
  def << edit;
  StatusBar bar(def);

  bar.setColumnEditText(0, QStringLiteral("typed"));
  QCOMPARE(childAt<QLineEdit>(bar, 0)->text(), QStringLiteral("typed"));
}

void TestStatusBar::testOutOfRangeIsNoOp() {
  StatusBarDef def;
  StatusBarColumn label;
  label.type = StatusBarColumnType::Label;
  def << label;
  StatusBar bar(def);

  // Must not crash; label text unchanged.
  bar.setColumnText(5, QStringLiteral("nope"));
  bar.setColumnText(-1, QStringLiteral("nope"));
  bar.setColumnVisible(99, false);
  QVERIFY(childAt<QLabel>(bar, 0)->text().isEmpty());
}

void TestStatusBar::testWrongTypeIsNoOp() {
  StatusBarDef def;
  StatusBarColumn label;
  label.type = StatusBarColumnType::Label;
  def << label;
  StatusBar bar(def);

  // Edit/menu/icon setters on a Label are safe no-ops.
  bar.setColumnEditText(0, QStringLiteral("x"));
  bar.setColumnMenuActions(0, QStringList{QStringLiteral("y")});
  bar.setColumnIcon(0, QIcon());
  QVERIFY(childAt<QLabel>(bar, 0)->text().isEmpty());
}

void TestStatusBar::testOnClickedFires() {
  int calls = 0;
  StatusBarDef def;
  StatusBarColumn button;
  button.type = StatusBarColumnType::Button;
  button.onClicked = [&calls]() { ++calls; };
  def << button;
  StatusBar bar(def);

  childAt<QToolButton>(bar, 0)->click();
  QCOMPARE(calls, 1);
}

void TestStatusBar::testOnTriggeredFires() {
  int triggered = -1;
  StatusBarDef def;
  StatusBarColumn menu;
  menu.type = StatusBarColumnType::Menu;
  menu.menuActions = QStringList{QStringLiteral("a"), QStringLiteral("b")};
  menu.onTriggered = [&triggered](int p_i) { triggered = p_i; };
  def << menu;
  StatusBar bar(def);

  childAt<QToolButton>(bar, 0)->menu()->actions().at(1)->trigger();
  QCOMPARE(triggered, 1);
}

void TestStatusBar::testOnTextEditedFires() {
  QString edited;
  StatusBarDef def;
  StatusBarColumn edit;
  edit.type = StatusBarColumnType::Edit;
  edit.onTextEdited = [&edited](const QString &p_t) { edited = p_t; };
  def << edit;
  StatusBar bar(def);

  auto *line = childAt<QLineEdit>(bar, 0);
  QTest::keyClicks(line, QStringLiteral("hi"));
  QCOMPARE(edited, QStringLiteral("hi"));
}

} // namespace tests

QTEST_MAIN(tests::TestStatusBar)
#include "test_statusbar.moc"
