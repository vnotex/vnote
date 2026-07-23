#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

#include <algorithm>

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
  void testWidgetColumnMountAndSwap();
  void testWidgetColumnNullClears();
  void testStructuredMenuBuildsCheckableAndSeparator();
  void testStructuredMenuExclusiveGroup();
  void testOnMenuTriggeredFires();
  void testShowMessageSwapsAndRestores();

private:
  // Find the Nth column child widget of type T in child/creation order. Columns
  // live under a host page inside the bar's QStackedLayout, so the search is
  // recursive; the internal transient message label is excluded so column
  // lookups stay stable.
  template <typename T> static T *childAt(StatusBar &p_bar, int p_n) {
    auto children = p_bar.findChildren<T *>();
    children.erase(std::remove_if(children.begin(), children.end(),
                                  [](T *p_w) {
                                    return p_w->objectName() ==
                                           QStringLiteral("StatusBarMessageLabel");
                                  }),
                   children.end());
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

void TestStatusBar::testWidgetColumnMountAndSwap() {
  StatusBarDef def;
  StatusBarColumn w;
  w.type = StatusBarColumnType::Widget;
  def << w;
  StatusBar bar(def);

  // Raw widget mounts (reparented under the bar).
  auto *raw = new QLabel(QStringLiteral("raw"));
  bar.setColumnWidget(0, raw);
  QVERIFY(bar.isAncestorOf(raw));

  // Shared widget swaps in and detaches the previous raw widget.
  auto shared = QSharedPointer<QWidget>::create();
  bar.setColumnWidget(0, shared);
  QVERIFY(bar.isAncestorOf(shared.data()));
  QVERIFY(!bar.isAncestorOf(raw));
  QCOMPARE(raw->parent(), nullptr);
  delete raw;

  // Shared pointer is kept alive while mounted.
  QVERIFY(!shared.isNull());
}

void TestStatusBar::testWidgetColumnNullClears() {
  StatusBarDef def;
  StatusBarColumn w;
  w.type = StatusBarColumnType::Widget;
  def << w;
  StatusBar bar(def);

  auto shared = QSharedPointer<QWidget>::create();
  bar.setColumnWidget(0, shared);
  QVERIFY(bar.isAncestorOf(shared.data()));

  // Null detaches and releases the shared widget; our local ref keeps it alive.
  bar.setColumnWidget(0, QSharedPointer<QWidget>());
  QVERIFY(!bar.isAncestorOf(shared.data()));
  QVERIFY(!shared.isNull());
}

void TestStatusBar::testStructuredMenuBuildsCheckableAndSeparator() {
  StatusBarDef def;
  StatusBarColumn menu;
  menu.type = StatusBarColumnType::Menu;
  QVector<StatusBarMenuItem> items;
  StatusBarMenuItem enable;
  enable.text = QStringLiteral("Enable");
  enable.checkable = true;
  enable.checked = true;
  StatusBarMenuItem sep;
  sep.separator = true;
  StatusBarMenuItem plain;
  plain.text = QStringLiteral("Plain");
  items << enable << sep << plain;
  menu.menuItems = items;
  def << menu;
  StatusBar bar(def);

  auto *btn = childAt<QToolButton>(bar, 0);
  QVERIFY(btn && btn->menu());
  const auto acts = btn->menu()->actions();
  QCOMPARE(acts.size(), 3);
  QVERIFY(acts.at(0)->isCheckable());
  QVERIFY(acts.at(0)->isChecked());
  QVERIFY(acts.at(1)->isSeparator());
  QVERIFY(!acts.at(2)->isCheckable());
}

void TestStatusBar::testStructuredMenuExclusiveGroup() {
  StatusBarDef def;
  StatusBarColumn menu;
  menu.type = StatusBarColumnType::Menu;
  QVector<StatusBarMenuItem> items;
  StatusBarMenuItem a;
  a.text = QStringLiteral("A");
  a.checkable = true;
  a.checked = true;
  a.exclusiveGroupId = 0;
  StatusBarMenuItem b;
  b.text = QStringLiteral("B");
  b.checkable = true;
  b.exclusiveGroupId = 0;
  items << a << b;
  menu.menuItems = items;
  def << menu;
  StatusBar bar(def);

  auto acts = childAt<QToolButton>(bar, 0)->menu()->actions();
  QCOMPARE(acts.size(), 2);
  QVERIFY(acts.at(0)->isChecked());
  // Exclusive group: checking B unchecks A.
  acts.at(1)->trigger();
  QVERIFY(acts.at(1)->isChecked());
  QVERIFY(!acts.at(0)->isChecked());
}

void TestStatusBar::testOnMenuTriggeredFires() {
  int firedIndex = -1;
  bool firedChecked = false;
  StatusBarDef def;
  StatusBarColumn menu;
  menu.type = StatusBarColumnType::Menu;
  QVector<StatusBarMenuItem> items;
  StatusBarMenuItem enable;
  enable.text = QStringLiteral("Enable");
  enable.checkable = true;
  StatusBarMenuItem sep;
  sep.separator = true;
  StatusBarMenuItem dict;
  dict.text = QStringLiteral("Dict");
  dict.checkable = true;
  items << enable << sep << dict;
  menu.menuItems = items;
  menu.onMenuTriggered = [&firedIndex, &firedChecked](int p_i, bool p_c) {
    firedIndex = p_i;
    firedChecked = p_c;
  };
  def << menu;
  StatusBar bar(def);

  // Item index 2 (dict) — separators keep their index slot.
  auto acts = childAt<QToolButton>(bar, 0)->menu()->actions();
  acts.at(2)->trigger();
  QCOMPARE(firedIndex, 2);
  QVERIFY(firedChecked);
}

void TestStatusBar::testShowMessageSwapsAndRestores() {
  StatusBarDef def;
  StatusBarColumn label;
  label.type = StatusBarColumnType::Label;
  label.text = QStringLiteral("L");
  def << label;
  StatusBar bar(def);
  bar.show();
  QVERIFY(QTest::qWaitForWindowExposed(&bar));

  // The single column label (message label is excluded by childAt).
  QLabel *columnLabel = childAt<QLabel>(bar, 0);
  QVERIFY(columnLabel);
  QVERIFY(columnLabel->isVisible());

  bar.showMessage(QStringLiteral("hi"), 100);
  QVERIFY(!columnLabel->isVisible());

  // Restored after the timer fires.
  QTRY_VERIFY_WITH_TIMEOUT(columnLabel->isVisible(), 2000);
}

} // namespace tests

QTEST_MAIN(tests::TestStatusBar)
#include "test_statusbar.moc"
