#include <QAccessible>
#include <QEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPixmap>
#include <QPointer>
#include <QProxyStyle>
#include <QStyle>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <QtTest>

#include <widgets/titlebarcontainer.h>

using namespace vnotex;

namespace tests {

namespace {

// Records which style elements a single widget asks for, so a test can tell a
// plain-widget paint apart from QMenuBar's panel/empty-area painting.
class RecordingStyle : public QProxyStyle {
public:
  // Default-constructed on purpose: QProxyStyle(QStyle *) would take ownership
  // of the application style.
  RecordingStyle() = default;

  void setWatched(const QWidget *p_widget) { m_watched = p_widget; }

  void drawPrimitive(PrimitiveElement p_element, const QStyleOption *p_option, QPainter *p_painter,
                     const QWidget *p_widget) const override {
    if (p_widget && p_widget == m_watched) {
      m_primitives.append(p_element);
    }
    QProxyStyle::drawPrimitive(p_element, p_option, p_painter, p_widget);
  }

  void drawControl(ControlElement p_element, const QStyleOption *p_option, QPainter *p_painter,
                   const QWidget *p_widget) const override {
    if (p_widget && p_widget == m_watched) {
      m_controls.append(p_element);
    }
    QProxyStyle::drawControl(p_element, p_option, p_painter, p_widget);
  }

  mutable QVector<PrimitiveElement> m_primitives;
  mutable QVector<ControlElement> m_controls;

private:
  const QWidget *m_watched = nullptr;
};

} // namespace

// Regression coverage for issue #2722: KDE Breeze's ToolsAreaManager calls
// QMainWindow::menuBar() on every main window it polishes, and Qt's accessor
// evicts + deleteLater()s whatever occupies the menu-widget slot when it is not
// a QMenuBar. That destroyed VNote's whole frameless title bar and left
// ToolBarHelper2 dereferencing freed QActions.
//
// These cases pin both halves of the fix: survival of the menuBar() accessor,
// and the QMenuBar behaviours (action-derived sizing, StyleChange self-resize,
// menu-bar painting, Alt-key navigation, accessibility role) that must NOT leak
// through.
//
// NOT GUILESS - real widgets need a QApplication.
class TestTitleBarContainer : public QObject {
  Q_OBJECT

private slots:
  void testSurvivesMenuBarAccessor();
  void testPlainWidgetIsEvicted();
  void testMenuSlotHeightFollowsLayout();
  void testMenuSlotHeightSurvivesStyleChange();
  void testPaintsAsPlainWidget();
  void testAltKeyDoesNotStealFocus();
  void testAccessibleInterfaceIsNotAnEmptyMenuBar();
  void testConstructorAddsNoActions();
  void testIsNotNativeMenuBar();

private:
  // The production shape from MainWindow2::setupToolBar(): a container holding a
  // fixed-height row with the same (0, 2, 0, 0) margins.
  static constexpr int c_childHeight = 40;
  static constexpr int c_topMargin = 2;

  static QWidget *populate(TitleBarContainer *p_container) {
    auto *layout = new QVBoxLayout(p_container);
    layout->setContentsMargins(0, c_topMargin, 0, 0);
    layout->setSpacing(0);

    auto *child = new QWidget(p_container);
    child->setFixedHeight(c_childHeight);
    layout->addWidget(child);
    return child;
  }
};

void TestTitleBarContainer::testSurvivesMenuBarAccessor() {
  QMainWindow mw;
  auto *container = new TitleBarContainer(&mw);
  mw.setMenuWidget(container);

  QPointer<QWidget> guard(container);

  // What Breeze does on polish.
  QVERIFY(mw.menuBar() != nullptr);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  QVERIFY2(!guard.isNull(), "TitleBarContainer was evicted and deleted by menuBar()");
  QCOMPARE(mw.menuWidget(), static_cast<QWidget *>(container));
}

void TestTitleBarContainer::testPlainWidgetIsEvicted() {
  // Negative control documenting the Qt behaviour the fix works around. If this
  // ever starts failing, Qt changed and the QMenuBar subclass may no longer be
  // necessary.
  QMainWindow mw;
  auto *plain = new QWidget(&mw);
  mw.setMenuWidget(plain);

  QPointer<QWidget> guard(plain);

  QVERIFY(mw.menuBar() != nullptr);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  QVERIFY2(guard.isNull(), "Qt no longer evicts a non-QMenuBar from the menu-widget slot");
}

void TestTitleBarContainer::testMenuSlotHeightFollowsLayout() {
  // The heightForWidth() pin. QMainWindowLayout has no menu-bar special-casing,
  // so QLayoutPrivate::doResize() sizes the slot from heightForWidth(); the
  // inherited QMenuBar implementation derives a height from actions and would
  // collapse the title bar. Assert the ACTUAL geometry, not just sizeHint().
  QMainWindow mw;
  auto *container = new TitleBarContainer(&mw);
  populate(container);
  mw.setMenuWidget(container);
  mw.resize(800, 600);
  mw.show();
  QVERIFY(QTest::qWaitForWindowExposed(&mw));

  QCOMPARE(mw.menuWidget(), static_cast<QWidget *>(container));
  QVERIFY(container->heightForWidth(800) >= c_childHeight + c_topMargin);
  QTRY_VERIFY2(container->height() >= c_childHeight + c_topMargin,
               qPrintable(QStringLiteral("title bar collapsed to %1px").arg(container->height())));
}

void TestTitleBarContainer::testMenuSlotHeightSurvivesStyleChange() {
  // The title bar must keep its height across a theme switch: VNote re-applies
  // the global stylesheet on every theme change, and QMenuBar::changeEvent()
  // self-resizes on QEvent::StyleChange.
  //
  // Honest scope: this passes with or without the changeEvent() override,
  // because that self-resize goes through the (overridden) heightForWidth().
  // It pins the user-visible outcome, not one specific override.
  QMainWindow mw;
  auto *container = new TitleBarContainer(&mw);
  populate(container);
  mw.setMenuWidget(container);
  mw.resize(800, 600);
  mw.show();
  QVERIFY(QTest::qWaitForWindowExposed(&mw));

  QEvent styleChange(QEvent::StyleChange);
  QCoreApplication::sendEvent(container, &styleChange);
  mw.setStyleSheet(QStringLiteral("QWidget { }"));
  QCoreApplication::processEvents();

  QVERIFY(container->heightForWidth(800) >= c_childHeight + c_topMargin);
  QTRY_VERIFY2(container->height() >= c_childHeight + c_topMargin,
               qPrintable(QStringLiteral("title bar collapsed to %1px after a style change")
                              .arg(container->height())));
}

void TestTitleBarContainer::testPaintsAsPlainWidget() {
  // The paintEvent() pin. QMenuBar::paintEvent() draws the menu-bar panel and
  // CE_MenuBarEmptyArea across the whole widget, which would sit behind the
  // title bar. The container must paint like an ordinary widget instead, and
  // must still issue PE_Widget so global QSS backgrounds keep working.
  //
  // Declared before the widgets so it outlives them.
  RecordingStyle style;

  QMainWindow mw;
  auto *container = new TitleBarContainer(&mw);
  populate(container);
  mw.setMenuWidget(container);
  mw.resize(800, 600);

  style.setWatched(container);
  container->setStyle(&style);

  // render() delivers a synchronous paint event; no show() needed.
  QPixmap canvas(container->size().isEmpty() ? QSize(800, 42) : container->size());
  canvas.fill(Qt::transparent);
  container->render(&canvas);

  QVERIFY2(style.m_primitives.contains(QStyle::PE_Widget),
           "Container did not draw PE_Widget, so global QSS backgrounds would be dropped");
  QVERIFY2(!style.m_primitives.contains(QStyle::PE_PanelMenuBar),
           "Container painted the menu bar panel behind the title bar");
  QVERIFY2(!style.m_controls.contains(QStyle::CE_MenuBarEmptyArea),
           "Container painted the menu bar empty area behind the title bar");

  // Detach before the recording style goes out of scope.
  container->setStyle(nullptr);
}

void TestTitleBarContainer::testAltKeyDoesNotStealFocus() {
  QMainWindow mw;
  auto *container = new TitleBarContainer(&mw);
  populate(container);
  mw.setMenuWidget(container);

  auto *edit = new QLineEdit(&mw);
  mw.setCentralWidget(edit);
  mw.resize(800, 600);
  mw.show();
  QVERIFY(QTest::qWaitForWindowExposed(&mw));

  if (!container->style()->styleHint(QStyle::SH_MenuBar_AltKeyNavigation, nullptr, container)) {
    QSKIP("Style has no Alt-key menu bar navigation");
  }

  edit->setFocus();
  QTRY_VERIFY(edit->hasFocus());

  // QMenuBar's event filter would enter keyboard mode here and take focus.
  QTest::keyPress(&mw, Qt::Key_Alt);
  QTest::keyRelease(&mw, Qt::Key_Alt);
  QCoreApplication::processEvents();

  QVERIFY2(!container->hasFocus(), "Alt moved focus into the title bar container");
  QCOMPARE(mw.focusWidget(), static_cast<QWidget *>(edit));
}

void TestTitleBarContainer::testAccessibleInterfaceIsNotAnEmptyMenuBar() {
  QMainWindow mw;
  auto *container = new TitleBarContainer(&mw);
  auto *child = populate(container);
  mw.setMenuWidget(container);

  // A missing Q_OBJECT would leave this as "QMenuBar" and the exact-class
  // accessibility factory would never match.
  QCOMPARE(QString::fromLatin1(container->metaObject()->className()),
           QStringLiteral("vnotex::TitleBarContainer"));

#if QT_CONFIG(accessibility)
  auto *iface = QAccessible::queryAccessibleInterface(container);
  QVERIFY(iface != nullptr);
  QVERIFY2(iface->role() != QAccessible::MenuBar,
           "Title bar container is exposed to assistive technology as a menu bar");

  bool foundChild = false;
  for (int i = 0; i < iface->childCount(); ++i) {
    auto *childIface = iface->child(i);
    if (childIface && childIface->object() == child) {
      foundChild = true;
      break;
    }
  }
  QVERIFY2(foundChild, "Accessibility tree does not expose the title bar's child widgets");
#else
  QSKIP("Qt built without accessibility support");
#endif
}

void TestTitleBarContainer::testConstructorAddsNoActions() {
  // The container invariant every override relies on. Scope: this only pins the
  // constructed state -- QMenuBar::addAction() stays publicly inherited, so a
  // future caller could still break the invariant. The production construction
  // path adds only a child layout, so runtime enforcement is not warranted.
  TitleBarContainer container;
  QVERIFY(container.actions().isEmpty());
}

void TestTitleBarContainer::testIsNotNativeMenuBar() {
  // Weak on Windows/headless CI, where this is already false without the
  // constructor call. It pins the platform invariant (the container must never
  // be hoisted into a global menu bar and hidden), not the call itself.
  TitleBarContainer container;
  QVERIFY(!container.isNativeMenuBar());
}

} // namespace tests

QTEST_MAIN(tests::TestTitleBarContainer)
#include "test_titlebarcontainer.moc"
