// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_contentfullscreenhost.cpp
//
// The widget reparenting behind ViewWindow2's content fullscreen (HTML5
// fullscreen from a web view, i.e. pdf.js's presentation mode).
//
// This lives in its own component precisely so it can be gated: no test
// compiles viewwindow2.cpp (it drags in the whole widget world), and
// reparenting is exactly the kind of code that fails as a stuck, blank or
// orphaned window rather than as a crash. Every case here is a failure that
// would otherwise only be found by a human noticing something odd on screen.
//
// NOT GUILESS: real windows, real layouts, real focus.

#include <QBoxLayout>
#include <QKeyEvent>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>

#include <widgets/contentfullscreenhost.h>

using vnotex::ContentFullScreenHost;

namespace tests {

namespace {

// A host window shaped like ViewWindow2: a vertical layout with a non-content
// widget above the stretching content, so a case can prove only the CONTENT
// travels.
struct Fixture {
  QWidget m_window;
  QVBoxLayout *m_layout = nullptr;
  QWidget *m_chrome = nullptr;
  QWidget *m_content = nullptr;

  Fixture() {
    m_layout = new QVBoxLayout(&m_window);
    m_chrome = new QWidget(&m_window);
    m_content = new QWidget(&m_window);
    m_layout->addWidget(m_chrome, 0);
    m_layout->addWidget(m_content, 1);
    m_window.resize(400, 300);
  }
};

} // namespace

class TestContentFullScreenHost : public QObject {
  Q_OBJECT

private slots:
  void aFreshHostIsNotFullScreen();
  void onlyTheContentTravels();
  void leavingPutsTheContentBackWithItsStretch();
  void leavingPutsTheContentBackAtItsOriginalIndex();
  void aRedundantToggleIsRefused();
  void enteringWithNothingToLiftIsRefused();
  void escapeIsReportedAsAnIntentNotAnExit();
  void escapeFromADescendantOfTheContentStillExits();
  void thereIsAlwaysAVisibleWayOut();
  void theExitLabelNamesTheModeNotTheMechanism();
  void escapeIsNotSwallowedOutsideTheContainer();
  void otherKeysAreNotSwallowed();
  void destroyingTheHostReturnsTheContent();
  void aDestroyedContentDoesNotCrashTheRestore();
};

void TestContentFullScreenHost::aFreshHostIsNotFullScreen() {
  ContentFullScreenHost host;
  QVERIFY(!host.isFullScreen());
  QVERIFY(!host.container());
  QVERIFY(!host.content());
  // Leaving when not in it is a no-op, not a crash: every teardown path calls
  // this unconditionally.
  QCOMPARE(host.exitFullScreen(), false);
}

// Only the CONTENT is lifted. Everything else -- toolbar, find bar, banners,
// status widget -- stays behind with the window; that is what makes this a
// *content* fullscreen rather than a window one.
void TestContentFullScreenHost::onlyTheContentTravels() {
  Fixture fixture;
  ContentFullScreenHost host;

  QVERIFY(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window));

  QVERIFY(host.isFullScreen());
  QVERIFY(host.container());
  QCOMPARE(host.content(), fixture.m_content);

  // The content now belongs to the container...
  QCOMPARE(fixture.m_content->parentWidget(), host.container());
  QVERIFY(fixture.m_layout->indexOf(fixture.m_content) < 0);
  // ...and the chrome did not move.
  QCOMPARE(fixture.m_chrome->parentWidget(), &fixture.m_window);
  QVERIFY(fixture.m_layout->indexOf(fixture.m_chrome) >= 0);

  // The container is a TOP-LEVEL parented to the owner window, so closing that
  // window cannot leave a fullscreen widget stranded on screen.
  QVERIFY(host.container()->isWindow());
  QCOMPARE(host.container()->parentWidget(), &fixture.m_window);
}

// Restoring with a hardcoded stretch would silently collapse a content widget
// to its size hint -- a window that comes back from fullscreen as a thin strip.
void TestContentFullScreenHost::leavingPutsTheContentBackWithItsStretch() {
  Fixture fixture;
  ContentFullScreenHost host;

  const int index = fixture.m_layout->indexOf(fixture.m_content);
  QCOMPARE(fixture.m_layout->stretch(index), 1);

  QVERIFY(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window));
  QVERIFY(host.exitFullScreen());

  QVERIFY(!host.isFullScreen());
  QCOMPARE(fixture.m_content->parentWidget(), &fixture.m_window);
  const int back = fixture.m_layout->indexOf(fixture.m_content);
  QVERIFY(back >= 0);
  QCOMPARE(fixture.m_layout->stretch(back), 1);

  // The container is gone rather than merely hidden.
  QPointer<QWidget> container = host.container();
  QVERIFY(container.isNull());
}

// Restoring by APPENDING would silently reorder a layout that has widgets on
// both sides of the content -- the fixture above cannot catch it, because its
// content is already last.
void TestContentFullScreenHost::leavingPutsTheContentBackAtItsOriginalIndex() {
  QWidget window;
  auto *layout = new QVBoxLayout(&window);
  auto *above = new QWidget(&window);
  auto *content = new QWidget(&window);
  auto *below = new QWidget(&window);
  layout->addWidget(above, 0);
  layout->addWidget(content, 1);
  layout->addWidget(below, 0);
  QCOMPARE(layout->indexOf(content), 1);

  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, content, layout, &window));
  QVERIFY(host.exitFullScreen());

  QCOMPARE(layout->indexOf(above), 0);
  QCOMPARE(layout->indexOf(content), 1);
  QCOMPARE(layout->indexOf(below), 2);
  QCOMPARE(layout->stretch(1), 1);
}

// A page and Qt can disagree about the current state; accepting a redundant
// toggle would reparent the view twice, which is how this ends up as a blank or
// orphaned fullscreen window.
void TestContentFullScreenHost::aRedundantToggleIsRefused() {
  Fixture fixture;
  ContentFullScreenHost host;

  QCOMPARE(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window), true);
  auto *first = host.container();
  QCOMPARE(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window), false);
  QCOMPARE(host.container(), first);

  QCOMPARE(host.exitFullScreen(), true);
  QCOMPARE(host.exitFullScreen(), false);
}

void TestContentFullScreenHost::enteringWithNothingToLiftIsRefused() {
  Fixture fixture;
  ContentFullScreenHost host;

  QCOMPARE(host.setFullScreen(true, nullptr, fixture.m_layout, &fixture.m_window), false);
  QVERIFY(!host.isFullScreen());
  QCOMPARE(host.setFullScreen(true, fixture.m_content, nullptr, &fixture.m_window), false);
  QVERIFY(!host.isFullScreen());
  // ...and the content was not disturbed by either refusal.
  QVERIFY(fixture.m_layout->indexOf(fixture.m_content) >= 0);
}

// Escape must NOT exit by itself. A web page has to be driven out through
// Chromium first, or it keeps believing it is fullscreen and rejects the next
// request -- presentation mode would then work exactly once per document.
void TestContentFullScreenHost::escapeIsReportedAsAnIntentNotAnExit() {
  Fixture fixture;
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window));

  QSignalSpy spy(&host, &ContentFullScreenHost::exitRequested);

  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QVERIFY(QCoreApplication::sendEvent(host.container(), &escape));

  QCOMPARE(spy.count(), 1);
  // Still fullscreen: the owner decides when to come back.
  QVERIFY(host.isFullScreen());
  QVERIFY(escape.isAccepted());
}

// THE regression case. The key press is delivered to the FOCUS widget, which
// for a QWebEngineView is Chromium's render widget several levels below the
// view -- and it consumes the event rather than letting it propagate up. A
// filter installed only on the container never sees it, which is exactly why
// the first version of presentation mode had no working exit at all.
void TestContentFullScreenHost::escapeFromADescendantOfTheContentStillExits() {
  Fixture fixture;
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window));

  // Stand in for the render widget: a grandchild of the content.
  auto *inner = new QWidget(fixture.m_content);
  auto *deepest = new QWidget(inner);

  QSignalSpy spy(&host, &ContentFullScreenHost::exitRequested);

  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(deepest, &escape);

  QCOMPARE(spy.count(), 1);
  // Swallowed, so the content never sees it -- the host owns Escape while
  // fullscreen.
  QVERIFY(escape.isAccepted());

  // ShortcutOverride is claimed too, so no QShortcut anywhere can steal Escape
  // before the key press arrives.
  QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier);
  QVERIFY(QCoreApplication::sendEvent(deepest, &override));
  QVERIFY(override.isAccepted());
}

// The window's own chrome stayed behind, so without this there is NOTHING on
// screen that ends fullscreen -- and a key that the content swallows is not a
// discoverable affordance even when it works.
void TestContentFullScreenHost::thereIsAlwaysAVisibleWayOut() {
  Fixture fixture;
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window));

  auto *button = host.exitButton();
  QVERIFY(button);
  QCOMPARE(button->parentWidget(), host.container());
  // It floats OVER the content rather than taking a strip of it.
  QVERIFY(host.container()->layout()->indexOf(button) < 0);
  // Focus must stay with the content, or the first keystroke of a presentation
  // would go to the button.
  QCOMPARE(button->focusPolicy(), Qt::NoFocus);
  QVERIFY(!button->text().isEmpty());

  QSignalSpy spy(&host, &ContentFullScreenHost::exitRequested);
  button->click();
  QCOMPARE(spy.count(), 1);
}

// The label must name the MODE the owner entered, not the mechanism: "Exit Full
// Screen" reads as the application's own full-screen toggle and sends the user
// looking at the View menu.
void TestContentFullScreenHost::theExitLabelNamesTheModeNotTheMechanism() {
  Fixture fixture;
  ContentFullScreenHost host;
  host.setExitButtonText(QStringLiteral("Exit Presentation Mode"));
  QVERIFY(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window));

  QCOMPARE(host.exitButton()->text(), QStringLiteral("Exit Presentation Mode"));
  // The key hint lives in the tooltip, so the visible label stays short but the
  // shortcut is still discoverable.
  QVERIFY(host.exitButton()->toolTip().contains(QStringLiteral("Esc")));
  QVERIFY(host.exitButton()->toolTip().contains(QStringLiteral("Exit Presentation Mode")));

  // Settable while already fullscreen, and an empty label is refused rather
  // than producing an unlabelled button nobody can identify.
  host.setExitButtonText(QStringLiteral("Leave Slideshow"));
  QCOMPARE(host.exitButton()->text(), QStringLiteral("Leave Slideshow"));
  host.setExitButtonText(QString());
  QCOMPARE(host.exitButton()->text(), QStringLiteral("Leave Slideshow"));
}

// The Escape filter is application-wide, so it MUST be scoped: swallowing
// Escape everywhere would break every dialog, popup and editor in the app.
void TestContentFullScreenHost::escapeIsNotSwallowedOutsideTheContainer() {
  Fixture fixture;
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window));

  QSignalSpy spy(&host, &ContentFullScreenHost::exitRequested);

  QWidget elsewhere;
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  escape.ignore();
  QCoreApplication::sendEvent(&elsewhere, &escape);

  QCOMPARE(spy.count(), 0);
  QVERIFY(!escape.isAccepted());

  // ...and once fullscreen ends, the filter is gone entirely.
  QVERIFY(host.exitFullScreen());
  QKeyEvent after(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  after.ignore();
  QCoreApplication::sendEvent(fixture.m_content, &after);
  QCOMPARE(spy.count(), 0);
  QVERIFY(!after.isAccepted());
}

void TestContentFullScreenHost::otherKeysAreNotSwallowed() {
  Fixture fixture;
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window));

  QSignalSpy spy(&host, &ContentFullScreenHost::exitRequested);

  QKeyEvent pageDown(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
  QCoreApplication::sendEvent(host.container(), &pageDown);

  QCOMPARE(spy.count(), 0);
  QVERIFY(host.isFullScreen());
}

// The content belongs to the CALLER. A host destroyed while fullscreen must not
// take it down with the container -- that would destroy a view window's editor.
void TestContentFullScreenHost::destroyingTheHostReturnsTheContent() {
  Fixture fixture;
  QPointer<QWidget> content = fixture.m_content;

  {
    ContentFullScreenHost host;
    QVERIFY(host.setFullScreen(true, fixture.m_content, fixture.m_layout, &fixture.m_window));
    QCOMPARE(fixture.m_content->parentWidget(), host.container());
  }

  QVERIFY(!content.isNull());
  QCOMPARE(content->parentWidget(), &fixture.m_window);
  QVERIFY(fixture.m_layout->indexOf(content) >= 0);
}

// The caller can be torn down from underneath (a view window closing while
// fullscreen). Raw pointers here would be a dangling reparent.
void TestContentFullScreenHost::aDestroyedContentDoesNotCrashTheRestore() {
  Fixture fixture;
  ContentFullScreenHost host;

  auto *content = new QWidget(&fixture.m_window);
  fixture.m_layout->addWidget(content, 1);
  QVERIFY(host.setFullScreen(true, content, fixture.m_layout, &fixture.m_window));

  delete content;

  QCOMPARE(host.exitFullScreen(), true);
  QVERIFY(!host.isFullScreen());
  QVERIFY(!host.container());
}

} // namespace tests

QTEST_MAIN(tests::TestContentFullScreenHost)
#include "test_contentfullscreenhost.moc"
