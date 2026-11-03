#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

#include <algorithm>

#include <widgets/editors/statusbar.h>
#include <widgets/editors/statusoverlay.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

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
  void testSetColumnMenuItems();
  void testSetColumnTextOnEdit();
  void testOutOfRangeIsNoOp();
  void testWrongTypeIsNoOp();
  void testOnClickedFires();
  void testOnTextEditedFires();
  void testWidgetColumnMountAndSwap();
  void testWidgetColumnNullClears();
  void testStructuredMenuBuildsCheckableAndSeparator();
  void testStructuredMenuExclusiveGroup();
  void testOnMenuTriggeredFires();
  void testShowMessageSwapsAndRestores();
  void testMessageVisibilitySignal();
  void testColumnIndexProperty();
  void testOverlayPreservesContentGeometry();
  void testFixedStatusReservesContentSpace();
  void testNativeOverlayInputAndLateContent_data();
  void testNativeOverlayInputAndLateContent();

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

void TestStatusBar::testSetColumnMenuItems() {
  StatusBarDef def;
  StatusBarColumn menu;
  menu.type = StatusBarColumnType::Menu;
  QVector<StatusBarMenuItem> items;
  StatusBarMenuItem a;
  a.text = QStringLiteral("a");
  StatusBarMenuItem b;
  b.text = QStringLiteral("b");
  items << a << b;
  menu.menuItems = items;
  def << menu;
  StatusBar bar(def);

  QToolButton *button = childAt<QToolButton>(bar, 0);
  QVERIFY(button && button->menu());
  QCOMPARE(button->menu()->actions().size(), 2);

  QVector<StatusBarMenuItem> replaced;
  StatusBarMenuItem x;
  x.text = QStringLiteral("x");
  replaced << x;
  bar.setColumnMenuItems(0, replaced);
  QCOMPARE(button->menu()->actions().size(), 1);
  QCOMPARE(button->menu()->actions().first()->text(), QStringLiteral("x"));
}

void TestStatusBar::testSetColumnTextOnEdit() {
  StatusBarDef def;
  StatusBarColumn edit;
  edit.type = StatusBarColumnType::Edit;
  def << edit;
  StatusBar bar(def);

  // setColumnText handles Edit columns.
  bar.setColumnText(0, QStringLiteral("typed"));
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

  // Menu/icon setters on a Label are safe no-ops.
  bar.setColumnMenuItems(0, QVector<StatusBarMenuItem>{});
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

void TestStatusBar::testMessageVisibilitySignal() {
  StatusBarDef def;
  StatusBarColumn label;
  label.type = StatusBarColumnType::Label;
  label.text = QStringLiteral("L");
  def << label;
  StatusBar bar(def);

  QSignalSpy spy(&bar, &StatusBar::messageVisibilityChanged);

  // Persistent message (0 timeout) -> one true, stays visible.
  bar.showMessage(QStringLiteral("persistent"), 0);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toBool(), true);
  QVERIFY(bar.isMessageVisible());

  // A stale timer from a prior message must not clear the persistent one.
  QTest::qWait(50);
  QVERIFY(bar.isMessageVisible());
  QCOMPARE(spy.count(), 0);

  // Explicit empty message clears -> one false.
  bar.showMessage(QString());
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toBool(), false);
  QVERIFY(!bar.isMessageVisible());

  // Timed message auto-clears via the timer -> emits false after timeout.
  bar.showMessage(QStringLiteral("timed"), 50);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toBool(), true);
  QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
  QCOMPARE(spy.takeFirst().at(0).toBool(), false);
  QVERIFY(!bar.isMessageVisible());
}

void TestStatusBar::testColumnIndexProperty() {
  StatusBarDef def;
  StatusBarColumn label;
  label.type = StatusBarColumnType::Label;
  label.text = QStringLiteral("L");
  StatusBarColumn spacer;
  spacer.type = StatusBarColumnType::Spacer;
  StatusBarColumn menu;
  menu.type = StatusBarColumnType::Menu;
  StatusBarColumn widget;
  widget.type = StatusBarColumnType::Widget;
  def << label << spacer << menu << widget;

  StatusBar bar(def);

  // The Label(0) and Menu(2) columns each carry a StatusBarCol property equal to
  // their index. The Spacer(1) has no widget, and the Widget(3) mount is
  // intentionally not tagged (it hosts a foreign, self-styled child).
  QList<int> indices;
  for (auto *w : bar.findChildren<QWidget *>()) {
    const QVariant v = w->property("StatusBarCol");
    if (v.isValid()) {
      indices << v.toInt();
    }
  }
  std::sort(indices.begin(), indices.end());
  QCOMPARE(indices, (QList<int>{0, 2}));
}

void TestStatusBar::testOverlayPreservesContentGeometry() {
  QWidget window;
  auto *layout = new QVBoxLayout(&window);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto *content = new QHBoxLayout;
  layout->addLayout(content, 1);
  auto *canvas = new QWidget(&window);
  content->addWidget(canvas);
  auto *bottom = new QVBoxLayout;
  layout->addLayout(bottom);
  auto *footer = new QLabel(QStringLiteral("Find"), &window);
  layout->addWidget(footer);
  StatusBarColumn column;
  column.text = QStringLiteral("Ready");
  auto *bar = new StatusBar({column}, &window);
  new StatusOverlay(bar, content, bottom, false);
  window.resize(420, 260);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  const QRect original = canvas->geometry();
  const QRect footerRect = footer->geometry();

  bar->hide();
  QCoreApplication::processEvents();
  QCOMPARE(canvas->geometry(), original);
  bar->showMessage(QStringLiteral("https://example.com/") + QString(4096, QLatin1Char('x')), 0);
  bar->show();
  QCoreApplication::processEvents();
  QCOMPARE(canvas->geometry(), original);
  QCOMPARE(footer->geometry(), footerRect);
  QCOMPARE(bar->geometry().bottom(), original.bottom());
  QCOMPARE(window.childAt(bar->geometry().center()), canvas);
  bar->showMessage(QStringLiteral("Saved"), 50);
  QTRY_VERIFY(!bar->isMessageVisible());
  QCOMPARE(canvas->geometry(), original);

  window.resize(260, 190);
  QTRY_COMPARE(bar->width(), canvas->width());
  QCOMPARE(bar->geometry().bottom(), canvas->geometry().bottom());
  const QRect resized = canvas->geometry();
  QFont font = bar->font();
  font.setPointSize(20);
  bar->setFont(font);
  QTRY_COMPARE(bar->height(), bar->sizeHint().height());
  QCOMPARE(canvas->geometry(), resized);
  QVERIFY(bar->geometry().bottom() < footer->geometry().top());

  // Shared status widgets can outlive their original host attachment.
  QWidget otherHost;
  bar->setParent(&otherHost);
  const QRect detached = bar->geometry();
  window.resize(480, 320);
  QCoreApplication::processEvents();
  QCOMPARE(bar->geometry(), detached);
}

void TestStatusBar::testFixedStatusReservesContentSpace() {
  QWidget window;
  auto *layout = new QVBoxLayout(&window);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto *content = new QHBoxLayout;
  layout->addLayout(content, 1);
  auto *canvas = new QWidget(&window);
  content->addWidget(canvas);
  auto *bottom = new QVBoxLayout;
  layout->addLayout(bottom);
  auto *footer = new QLabel(QStringLiteral("Find"), &window);
  layout->addWidget(footer);
  window.resize(420, 260);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  const int minimumWidth = window.minimumSizeHint().width();

  StatusBarColumn column;
  column.text = QString(256, QLatin1Char('W'));
  auto *bar = new StatusBar({column}, &window);
  auto *attachment = new StatusOverlay(bar, content, bottom, true);
  bar->show();
  QTRY_COMPARE(canvas->geometry().bottom() + 1, bar->geometry().top());
  QCOMPARE(bar->geometry().bottom() + 1, footer->geometry().top());
  QVERIFY(!canvas->geometry().intersects(bar->geometry()));
  QCOMPARE(window.minimumSizeHint().width(), minimumWidth);
  window.resize(220, 180);
  QTRY_COMPARE(window.width(), 220);
  QTRY_COMPARE(bar->width(), canvas->width());
  QCOMPARE(canvas->geometry().bottom() + 1, bar->geometry().top());

  const QRect original = canvas->geometry();
  const int messageMinimumWidth = window.minimumSizeHint().width();
  const QString url = QStringLiteral("https://example.com/") + QString(4096, QLatin1Char('x'));
  bar->showMessage(url, 0);
  QCoreApplication::processEvents();
  QCOMPARE(canvas->geometry(), original);
  QCOMPARE(window.minimumSizeHint().width(), messageMinimumWidth);
  bar->showMessage(QString());
  QCoreApplication::processEvents();
  QCOMPARE(canvas->geometry(), original);
  QCOMPARE(window.minimumSizeHint().width(), messageMinimumWidth);
  bar->showMessage(url, 100);
  QTRY_VERIFY(!bar->isMessageVisible());
  QCOMPARE(canvas->geometry(), original);
  QCOMPARE(window.minimumSizeHint().width(), messageMinimumWidth);

  bar->hide();
  QTRY_COMPARE(canvas->geometry().bottom() + 1, footer->geometry().top());
  bar->show();
  QTRY_COMPARE(canvas->geometry(), original);
  window.hide();
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QTRY_COMPARE(canvas->geometry(), original);

  attachment->setFixed(false);
  attachment->setFixed(false);
  QTRY_COMPARE(canvas->geometry().bottom() + 1, footer->geometry().top());
  QCOMPARE(bar->geometry().bottom(), canvas->geometry().bottom());
  QVERIFY(canvas->geometry().intersects(bar->geometry()));
  attachment->setFixed(true);
  attachment->setFixed(true);
  QTRY_COMPARE(canvas->geometry(), original);
  window.resize(480, 320);
  QTRY_COMPARE(canvas->geometry().bottom() + 1, bar->geometry().top());
  QCOMPARE(bar->geometry().bottom() + 1, footer->geometry().top());
  QFont font = bar->font();
  font.setPointSize(20);
  bar->setFont(font);
  QTRY_COMPARE(bar->height(), bar->sizeHint().height());
  QCOMPARE(canvas->geometry().bottom() + 1, bar->geometry().top());
  QCOMPARE(bar->geometry().bottom() + 1, footer->geometry().top());
  QVERIFY(!canvas->geometry().intersects(bar->geometry()));

  QWidget otherHost;
  bar->setParent(&otherHost);
  const QRect detached = bar->geometry();
  attachment->setFixed(false);
  attachment->setFixed(true);
  window.resize(520, 360);
  QTRY_COMPARE(canvas->geometry().bottom() + 1, footer->geometry().top());
  QCOMPARE(bar->parentWidget(), &otherHost);
  QCOMPARE(bar->geometry(), detached);
}

void TestStatusBar::testNativeOverlayInputAndLateContent_data() {
  QTest::addColumn<bool>("nested");
  QTest::newRow("top-level") << false;
  QTest::newRow("alien-tab") << true;
}

void TestStatusBar::testNativeOverlayInputAndLateContent() {
#ifndef Q_OS_WIN
  QSKIP("Windows native-child hit testing");
#else
  const bool previous = QCoreApplication::testAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
  QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
  const auto restore = qScopeGuard(
      [&] { QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, previous); });
  QFETCH(bool, nested);
  QWidget window;
  QWidget *view = &window;
  QPushButton *outside = nullptr;
  int outsideClicks = 0;
  if (nested) {
    auto *shell = new QVBoxLayout(&window);
    outside = new QPushButton(QStringLiteral("Other pane"), &window);
    shell->addWidget(outside);
    connect(outside, &QPushButton::clicked, this, [&] { ++outsideClicks; });
    view = new QWidget(&window);
    shell->addWidget(view, 1);
  }
  auto *layout = new QVBoxLayout(view);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto *content = new QHBoxLayout;
  layout->addLayout(content, 1);
  auto *canvas = new QWidget(view);
  content->addWidget(canvas);
  auto *bottom = new QVBoxLayout;
  layout->addLayout(bottom);
  int controlClicks = 0;
  StatusBarColumn column;
  column.type = StatusBarColumnType::Button;
  column.text = QStringLiteral("Encoding");
  column.onClicked = [&] { ++controlClicks; };
  auto *bar = new StatusBar({column}, view);
  auto *attachment = new StatusOverlay(bar, content, bottom, true);
  window.resize(400, 240);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));

  // WebEngine's HWND appears after status construction in Markdown Read mode.
  auto *native = new QPushButton(QStringLiteral("Content link"), canvas);
  native->setAttribute(Qt::WA_DontCreateNativeAncestors);
  native->setAttribute(Qt::WA_NativeWindow);
  auto *nativeLayout = new QVBoxLayout(canvas);
  nativeLayout->setContentsMargins(0, 0, 0, 0);
  nativeLayout->setSpacing(0);
  nativeLayout->addWidget(native);
  native->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  native->show();
  QSignalSpy contentClicks(native, &QPushButton::clicked);
  QCoreApplication::processEvents();
  auto *button = bar->findChild<QToolButton *>();
  QVERIFY(button);
  auto clickAt = [&](const QPoint &p_position) {
    const qreal ratio = window.devicePixelRatioF();
    POINT point{qRound(p_position.x() * ratio), qRound(p_position.y() * ratio)};
    HWND target = reinterpret_cast<HWND>(window.winId());
    while (true) {
      HWND child = ChildWindowFromPointEx(
          target, point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
      if (!child || child == target) {
        break;
      }
      MapWindowPoints(target, child, &point, 1);
      target = child;
    }
    const auto position = MAKELPARAM(point.x, point.y);
    PostMessage(target, WM_LBUTTONDOWN, MK_LBUTTON, position);
    PostMessage(target, WM_LBUTTONUP, 0, position);
  };
  auto clickCovered = [&] { clickAt(button->mapTo(&window, button->rect().center())); };
  clickCovered();
  QTRY_COMPARE(controlClicks, 1);
  QCOMPARE(contentClicks.count(), 0);
  bar->hide();
  attachment->setFixed(false);
  bar->showMessage(QStringLiteral("https://example.com/"), 0);
  bar->show();
  QCoreApplication::processEvents();
  QCOMPARE(native->geometry(), canvas->rect());
  clickAt(bar->mapTo(&window, bar->rect().center()));
  QTRY_COMPARE(contentClicks.count(), 1);
  QCOMPARE(controlClicks, 1);
  bar->showMessage(QString());
  attachment->setFixed(true);
  QCoreApplication::processEvents();
  clickCovered();
  QTRY_COMPARE(controlClicks, 2);
  QCOMPARE(contentClicks.count(), 1);
  if (outside) {
    for (const QSize size : {QSize(560, 320), QSize(300, 180)}) {
      view->hide();
      window.resize(size);
      view->show();
      QCoreApplication::processEvents();
      native->setGeometry(canvas->rect());
      for (bool fixed : {false, true}) {
        attachment->setFixed(fixed);
        QCoreApplication::processEvents();
        QCOMPARE(bar->width(), canvas->width());
        if (fixed) {
          QCOMPARE(canvas->geometry().bottom() + 1, bar->geometry().top());
          QVERIFY(!canvas->geometry().intersects(bar->geometry()));
        } else {
          QVERIFY(canvas->geometry().contains(bar->geometry()));
        }
        RECT bounds;
        QVERIFY(GetWindowRect(reinterpret_cast<HWND>(bar->winId()), &bounds));
        const auto *area = fixed ? view : canvas;
        const QPoint origin = area->mapToGlobal(QPoint());
        const qreal ratio = window.devicePixelRatioF();
        // Native origins and sizes are rounded independently at fractional display scaling.
        const QPoint nativeOrigin(qRound(origin.x() * ratio), qRound(origin.y() * ratio));
        QVERIFY(bounds.left >= nativeOrigin.x());
        QVERIFY(bounds.top >= nativeOrigin.y());
        QVERIFY(bounds.right <= nativeOrigin.x() + qRound(area->width() * ratio));
        QVERIFY(bounds.bottom <= nativeOrigin.y() + qRound(area->height() * ratio));
        const int expectedClicks = outsideClicks + 1;
        clickAt(outside->mapTo(&window, outside->rect().center()));
        QTRY_COMPARE(outsideClicks, expectedClicks);
        QCOMPARE(controlClicks, 2);
      }
    }
    clickCovered();
    QTRY_COMPARE(controlClicks, 3);
    QCOMPARE(contentClicks.count(), 1);
  }
#endif
}

} // namespace tests

QTEST_MAIN(tests::TestStatusBar)
#include "test_statusbar.moc"
