// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Whole-ViewWindow fullscreen promotion: real tab identity, native flags,
// layouts, focus and exit events. NOT GUILESS: this is the native-window gate.

#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QSignalSpy>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>
#include <QtTest>

#include <widgets/contentfullscreenhost.h>

using vnotex::ContentFullScreenHost;

namespace tests {

namespace {

struct Fixture {
  QTabWidget m_tabs;
  QLineEdit *m_before = new QLineEdit;
  QWidget *m_page = new QWidget;
  QLineEdit *m_after = new QLineEdit;
  QVBoxLayout *m_layout = new QVBoxLayout(m_page);
  QToolBar *m_toolbar = new QToolBar(m_page);
  QWidget *m_content = new QWidget(m_page);
  QLineEdit *m_editor = nullptr;
  QLineEdit *m_find = new QLineEdit(m_page);

  Fixture() {
    // Match ViewWindow2 rather than platform-dependent child/window defaults.
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_toolbar->addAction(QStringLiteral("Presentation Mode"));
    auto *contentLayout = new QVBoxLayout(m_content);
    auto *inner = new QWidget(m_content);
    auto *innerLayout = new QVBoxLayout(inner);
    m_editor = new QLineEdit(inner);
    innerLayout->addWidget(m_editor);
    contentLayout->addWidget(inner);
    m_layout->addWidget(m_toolbar);
    m_layout->addWidget(m_content, 1);
    m_layout->addWidget(m_find);
    m_page->setFocusProxy(m_editor);
    m_page->setWindowFlag(Qt::CustomizeWindowHint);
    m_page->setAttribute(Qt::WA_QuitOnClose, true);
    m_tabs.addTab(m_before, QStringLiteral("Before"));
    m_tabs.addTab(m_page, QStringLiteral("Document"));
    m_tabs.addTab(m_after, QStringLiteral("After"));
    m_tabs.setCurrentWidget(m_page);
    m_tabs.resize(620, 420);
  }

  bool show() {
    m_tabs.show();
    m_tabs.raise();
    m_tabs.activateWindow();
    return QTest::qWaitForWindowExposed(&m_tabs);
  }
};

void connectExitConsumer(ContentFullScreenHost &p_host) {
  QObject::connect(&p_host, &ContentFullScreenHost::exitRequested, &p_host,
                   [&p_host]() { p_host.exitFullScreen(); });
}

} // namespace

class TestContentFullScreenHost : public QObject {
  Q_OBJECT

private slots:
  // Keep this first: the replacement depends on Qt preserving this exact page.
  void theWholeTabKeepsItsIdentity();
  void aDisabledParentLayoutStaysDisabled();
  void aParentWithoutALayoutIsSupported();
  void aFreshHostIsNotFullScreen();
  void aRedundantToggleIsRefused();
  void invalidEntryDoesNotChangeTheWidget();
  void escapeIsReportedAsAnIntentNotAnExit();
  void escapeFromADeepDescendantClaimsShortcutOverride();
  void anOwnedPopupHandlesTheFirstEscape();
  void escapeLeavesUnrelatedWidgetsAndChildDialogsAlone();
  void nativeWindowEscapeUsesTheActivePresentation();
  void otherKeysReachTheContent();
  void nativeCloseExitsWithoutClosingTheTab();
  void switchingTabsExitsWithoutShowingOrFocusingTheOldTab();
  void ownershipTransferRestoresUnderTheNewParent();
  void anOldHideCannotExitANewPresentation();
  void losingFullScreenExitsButMinimizingDoesNot();
  void aRemovedFocusTargetFallsBackToTheExistingProxy();
  void destroyingTheHostRestoresThePage();
  void destroyingThePageRestoresTheSurvivingLayout();
  void destroyingTheOwnerLeavesNoActiveHost();
};

void TestContentFullScreenHost::theWholeTabKeepsItsIdentity() {
  Fixture fixture;
  QVERIFY(fixture.show());
  auto *page = fixture.m_page;
  auto *parent = page->parentWidget();
  auto *parentLayout = parent->layout();
  QVERIFY(parentLayout);
  QVERIFY(parentLayout->isEnabled());
  const auto flags = page->windowFlags();
  const auto state = page->windowState();
  const auto geometry = page->geometry();
  const auto toolbarGeometry = fixture.m_toolbar->geometry();
  const auto contentGeometry = fixture.m_content->geometry();
  const auto findGeometry = fixture.m_find->geometry();
  const auto screen = page->screen();
  const auto topLevels = QApplication::topLevelWidgets();
  QSignalSpy tabChanges(&fixture.m_tabs, &QTabWidget::currentChanged);
  ContentFullScreenHost host;
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);

  for (int pass = 0; pass < 2; ++pass) {
    fixture.m_find->setFocus();
    QTRY_VERIFY(fixture.m_find->hasFocus());
    QVERIFY(host.setFullScreen(true, page));
    QVERIFY(QTest::qWaitForWindowExposed(page));
    // Check the native surface: offscreen can retain a stale QWidget frame offset.
    QTRY_COMPARE(page->windowHandle()->geometry(), screen->geometry());
    QCOMPARE(page->size(), screen->geometry().size());
    QVERIFY(page->isFullScreen());
    QVERIFY(page->isWindow());
    QVERIFY(host.isFullScreen());
    QCOMPARE(host.content(), page);
    QCOMPARE(page->parentWidget(), parent);
    QCOMPARE(page->screen(), screen);
    QVERIFY(!page->testAttribute(Qt::WA_QuitOnClose));
    QVERIFY(!parentLayout->isEnabled());
    QVERIFY(fixture.m_layout->isEnabled());
    QVERIFY(fixture.m_toolbar->isVisible());
    QVERIFY(fixture.m_find->isVisible());
    QCOMPARE(fixture.m_layout->indexOf(fixture.m_toolbar), 0);
    QCOMPARE(fixture.m_layout->indexOf(fixture.m_content), 1);
    QCOMPARE(fixture.m_layout->indexOf(fixture.m_find), 2);
    QVERIFY(fixture.m_toolbar->geometry().bottom() < fixture.m_content->geometry().top());
    QVERIFY(fixture.m_content->geometry().bottom() < fixture.m_find->geometry().top());
    QTRY_VERIFY(fixture.m_editor->hasFocus());
    for (auto *window : QApplication::topLevelWidgets()) {
      QVERIFY(window == page || topLevels.contains(window));
    }

    // A background QStackedLayout pass must not resize the promoted current page.
    fixture.m_tabs.resize(740 + pass * 60, 520 + pass * 40);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QCoreApplication::processEvents();
    QCOMPARE(page->windowHandle()->geometry(), screen->geometry());
    QCOMPARE(page->size(), screen->geometry().size());
    QCOMPARE(fixture.m_tabs.count(), 3);
    QCOMPARE(fixture.m_tabs.indexOf(page), 1);
    QCOMPARE(fixture.m_tabs.widget(0), fixture.m_before);
    QCOMPARE(fixture.m_tabs.widget(1), page);
    QCOMPARE(fixture.m_tabs.widget(2), fixture.m_after);
    QCOMPARE(fixture.m_tabs.tabText(0), QStringLiteral("Before"));
    QCOMPARE(fixture.m_tabs.tabText(1), QStringLiteral("Document"));
    QCOMPARE(fixture.m_tabs.tabText(2), QStringLiteral("After"));
    QCOMPARE(fixture.m_tabs.currentWidget(), page);
    QCOMPARE(tabChanges.count(), 0);
    QCOMPARE(exits.count(), 0);

    QVERIFY(host.exitFullScreen());
    QVERIFY(!host.isFullScreen());
    QVERIFY(!host.content());
    QVERIFY(!page->isWindow());
    QVERIFY(page->isVisible());
    QCOMPARE(page->parentWidget(), parent);
    QCOMPARE(page->windowFlags(), flags);
    QCOMPARE(page->windowState(), state);
    QVERIFY(page->testAttribute(Qt::WA_QuitOnClose));
    QVERIFY(parentLayout->isEnabled());
    QTRY_COMPARE(page->geometry(), parent->contentsRect());
    QTRY_VERIFY(fixture.m_find->hasFocus());
    QCOMPARE(fixture.m_tabs.currentWidget(), page);
    QCOMPARE(fixture.m_tabs.count(), 3);
    QCOMPARE(fixture.m_tabs.indexOf(page), 1);
    QCOMPARE(tabChanges.count(), 0);

    fixture.m_tabs.resize(620, 420);
    QTRY_COMPARE(page->geometry(), geometry);
    QTRY_COMPARE(fixture.m_toolbar->geometry(), toolbarGeometry);
    QCOMPARE(fixture.m_content->geometry(), contentGeometry);
    QCOMPARE(fixture.m_find->geometry(), findGeometry);
  }
}

void TestContentFullScreenHost::aDisabledParentLayoutStaysDisabled() {
  Fixture fixture;
  QVERIFY(fixture.show());
  auto *layout = fixture.m_page->parentWidget()->layout();
  layout->setEnabled(false);
  const auto geometry = fixture.m_page->geometry();
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  fixture.m_tabs.resize(810, 570);
  QVERIFY(host.exitFullScreen());
  QCoreApplication::processEvents();
  QVERIFY(!layout->isEnabled());
  QCOMPARE(fixture.m_page->geometry(), geometry);
  QVERIFY(fixture.m_page->isVisible());
}

void TestContentFullScreenHost::aParentWithoutALayoutIsSupported() {
  QWidget parent;
  QWidget page(&parent);
  parent.resize(400, 300);
  page.setGeometry(30, 40, 200, 150);
  parent.show();
  QVERIFY(QTest::qWaitForWindowExposed(&parent));
  const auto geometry = page.geometry();
  const auto flags = page.windowFlags();
  page.setAttribute(Qt::WA_QuitOnClose, false);
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, &page));
  QVERIFY(host.exitFullScreen());
  QCOMPARE(page.parentWidget(), &parent);
  QCOMPARE(page.geometry(), geometry);
  QCOMPARE(page.windowFlags(), flags);
  QVERIFY(!page.testAttribute(Qt::WA_QuitOnClose));
  QVERIFY(page.isVisible());
}

void TestContentFullScreenHost::aFreshHostIsNotFullScreen() {
  ContentFullScreenHost host;
  QVERIFY(!host.isFullScreen());
  QVERIFY(!host.content());
  QVERIFY(!host.exitFullScreen());
}

void TestContentFullScreenHost::aRedundantToggleIsRefused() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  const auto flags = fixture.m_page->windowFlags();
  QVERIFY(!host.setFullScreen(true, fixture.m_page));
  QVERIFY(!host.setFullScreen(true, fixture.m_before));
  QCOMPARE(host.content(), fixture.m_page);
  QCOMPARE(fixture.m_page->windowFlags(), flags);
  QVERIFY(host.exitFullScreen());
  QVERIFY(!host.exitFullScreen());
}

void TestContentFullScreenHost::invalidEntryDoesNotChangeTheWidget() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  const auto flags = fixture.m_before->windowFlags();
  auto *parent = fixture.m_before->parentWidget();
  QVERIFY(!host.setFullScreen(true, nullptr));
  QVERIFY(!host.setFullScreen(true, fixture.m_before));
  QVERIFY(!host.setFullScreen(true, &fixture.m_tabs));
  QWidget childWindow(fixture.m_page, Qt::Window);
  childWindow.show();
  QVERIFY(!host.setFullScreen(true, &childWindow));
  QVERIFY(!host.isFullScreen());
  QVERIFY(!host.content());
  QCOMPARE(fixture.m_before->windowFlags(), flags);
  QCOMPARE(fixture.m_before->parentWidget(), parent);
  QCOMPARE(fixture.m_tabs.currentWidget(), fixture.m_page);
  QVERIFY(parent->layout()->isEnabled());
}

void TestContentFullScreenHost::escapeIsReportedAsAnIntentNotAnExit() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(fixture.m_page, &escape);
  QCOMPARE(exits.count(), 1);
  QVERIFY(escape.isAccepted());
  QVERIFY(host.isFullScreen());
}

void TestContentFullScreenHost::escapeFromADeepDescendantClaimsShortcutOverride() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier);
  override.ignore();
  QCoreApplication::sendEvent(fixture.m_editor, &override);
  QVERIFY(override.isAccepted());
  QCOMPARE(exits.count(), 0);
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  escape.ignore();
  QCoreApplication::sendEvent(fixture.m_editor, &escape);
  QCOMPARE(exits.count(), 1);
  QVERIFY(escape.isAccepted());
}

void TestContentFullScreenHost::anOwnedPopupHandlesTheFirstEscape() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  connectExitConsumer(host);
  QMenu menu(fixture.m_toolbar);
  menu.addAction(QStringLiteral("Page"));
  menu.popup(fixture.m_toolbar->mapToGlobal(QPoint(0, fixture.m_toolbar->height())));
  QTRY_VERIFY(menu.isVisible());
  QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier);
  override.ignore();
  QCoreApplication::sendEvent(fixture.m_page, &override);
  QVERIFY(!override.isAccepted());
  QTest::keyClick(&menu, Qt::Key_Escape);
  QTRY_VERIFY(!menu.isVisible());
  QCOMPARE(exits.count(), 0);
  QVERIFY(host.isFullScreen());
  QTest::keyClick(fixture.m_editor, Qt::Key_Escape);
  QCOMPARE(exits.count(), 1);
  QVERIFY(!host.isFullScreen());
}

void TestContentFullScreenHost::escapeLeavesUnrelatedWidgetsAndChildDialogsAlone() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  QWidget elsewhere;
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  escape.ignore();
  QCoreApplication::sendEvent(&elsewhere, &escape);
  QVERIFY(!escape.isAccepted());

  QDialog dialog(fixture.m_page);
  dialog.open();
  QTRY_VERIFY(dialog.isVisible());
  QTest::keyClick(&dialog, Qt::Key_Escape);
  QTRY_VERIFY(!dialog.isVisible());
  QCOMPARE(exits.count(), 0);
  QVERIFY(host.isFullScreen());

  QVERIFY(host.exitFullScreen());
  QKeyEvent after(QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier);
  after.ignore();
  QCoreApplication::sendEvent(fixture.m_page, &after);
  QVERIFY(!after.isAccepted());
  QCOMPARE(exits.count(), 0);
}

void TestContentFullScreenHost::nativeWindowEscapeUsesTheActivePresentation() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  QTRY_VERIFY(fixture.m_page->isActiveWindow());
  QVERIFY(fixture.m_page->windowHandle());
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(fixture.m_page->windowHandle(), &escape);
  QCOMPARE(exits.count(), 1);
  QVERIFY(escape.isAccepted());
}

void TestContentFullScreenHost::otherKeysReachTheContent() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  QTest::keyClicks(fixture.m_editor, QStringLiteral("page"));
  QCOMPARE(fixture.m_editor->text(), QStringLiteral("page"));
  QCOMPARE(exits.count(), 0);
  QVERIFY(host.isFullScreen());
}

void TestContentFullScreenHost::nativeCloseExitsWithoutClosingTheTab() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  QPointer<QWidget> page = fixture.m_page;
  page->setAttribute(Qt::WA_DeleteOnClose, true);
  const auto flags = page->windowFlags();
  QVERIFY(host.setFullScreen(true, page));
  connectExitConsumer(host);
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  QVERIFY(page->windowHandle());
  page->windowHandle()->close();
  QTRY_VERIFY(!host.isFullScreen());
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(page);
  QVERIFY(page->isVisible());
  QCOMPARE(page->windowFlags(), flags);
  QCOMPARE(fixture.m_tabs.currentWidget(), page.data());
  QCOMPARE(fixture.m_tabs.count(), 3);
  QCOMPARE(fixture.m_tabs.indexOf(page), 1);
  QCOMPARE(exits.count(), 1);
}

void TestContentFullScreenHost::switchingTabsExitsWithoutShowingOrFocusingTheOldTab() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  const auto flags = fixture.m_page->windowFlags();
  auto *parent = fixture.m_page->parentWidget();
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  connectExitConsumer(host);
  fixture.m_tabs.setCurrentWidget(fixture.m_after);
  fixture.m_tabs.activateWindow();
  fixture.m_after->setFocus();
  QTRY_VERIFY(!host.isFullScreen());
  QTRY_VERIFY(fixture.m_after->hasFocus());
  QVERIFY(!fixture.m_page->isVisible());
  QCOMPARE(fixture.m_page->windowFlags(), flags);
  QCOMPARE(fixture.m_page->parentWidget(), parent);
  QVERIFY(parent->layout()->isEnabled());
  QCOMPARE(fixture.m_tabs.currentWidget(), fixture.m_after);
  QCOMPARE(fixture.m_tabs.count(), 3);
  QCOMPARE(fixture.m_tabs.indexOf(fixture.m_page), 1);
}

void TestContentFullScreenHost::ownershipTransferRestoresUnderTheNewParent() {
  Fixture fixture;
  QVERIFY(fixture.show());
  QTabWidget destination;
  destination.addTab(new QWidget, QStringLiteral("Destination"));
  destination.resize(460, 340);
  destination.show();
  QVERIFY(QTest::qWaitForWindowExposed(&destination));
  ContentFullScreenHost host;
  auto *oldParent = fixture.m_page->parentWidget();
  const auto flags = fixture.m_page->windowFlags();
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  connectExitConsumer(host);
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);

  fixture.m_tabs.removeTab(fixture.m_tabs.indexOf(fixture.m_page));
  fixture.m_page->hide();
  destination.addTab(fixture.m_page, QStringLiteral("Document"));
  destination.setCurrentWidget(fixture.m_page);
  destination.activateWindow();
  fixture.m_editor->setFocus();
  QTRY_VERIFY(!host.isFullScreen());
  QCoreApplication::processEvents();
  QCOMPARE(exits.count(), 1);
  QVERIFY(fixture.m_page->parentWidget() != oldParent);
  QCOMPARE(fixture.m_page->windowFlags(), flags);
  QVERIFY(oldParent->layout()->isEnabled());
  QCOMPARE(destination.currentWidget(), fixture.m_page);
  QCOMPARE(destination.indexOf(fixture.m_page), 1);
  QCOMPARE(destination.count(), 2);
  QCOMPARE(fixture.m_tabs.indexOf(fixture.m_page), -1);
  QCOMPARE(fixture.m_tabs.count(), 2);
  QVERIFY(fixture.m_page->isVisible());
  QTRY_COMPARE(fixture.m_page->geometry(), fixture.m_page->parentWidget()->contentsRect());
  QTRY_VERIFY(fixture.m_editor->hasFocus());
}

void TestContentFullScreenHost::anOldHideCannotExitANewPresentation() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  connectExitConsumer(host);
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  fixture.m_page->hide();
  QVERIFY(host.exitFullScreen());
  fixture.m_page->show();
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  QCoreApplication::processEvents();
  QVERIFY(host.isFullScreen());
  QCOMPARE(exits.count(), 0);
}

void TestContentFullScreenHost::losingFullScreenExitsButMinimizingDoesNot() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  const auto flags = fixture.m_page->windowFlags();
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  connectExitConsumer(host);
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  fixture.m_page->setWindowState(Qt::WindowMinimized);
  QCoreApplication::processEvents();
  QVERIFY(host.isFullScreen());
  QCOMPARE(exits.count(), 0);
  fixture.m_page->setWindowState(Qt::WindowNoState);
  QTRY_VERIFY(!host.isFullScreen());
  QCOMPARE(exits.count(), 1);
  QCOMPARE(fixture.m_page->windowFlags(), flags);
  QVERIFY(fixture.m_page->isVisible());
}

void TestContentFullScreenHost::aRemovedFocusTargetFallsBackToTheExistingProxy() {
  Fixture fixture;
  QVERIFY(fixture.show());
  fixture.m_find->setFocus();
  QTRY_VERIFY(fixture.m_find->hasFocus());
  ContentFullScreenHost host;
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  QWidget otherOwner;
  fixture.m_find->setParent(&otherOwner);
  QVERIFY(host.exitFullScreen());
  QTRY_VERIFY(fixture.m_editor->hasFocus());
}

void TestContentFullScreenHost::destroyingTheHostRestoresThePage() {
  Fixture fixture;
  QVERIFY(fixture.show());
  QPointer<QWidget> page = fixture.m_page;
  auto *parent = page->parentWidget();
  const auto flags = page->windowFlags();
  {
    ContentFullScreenHost host;
    QVERIFY(host.setFullScreen(true, page));
  }
  QVERIFY(page);
  QCOMPARE(page->windowFlags(), flags);
  QCOMPARE(page->parentWidget(), parent);
  QVERIFY(parent->layout()->isEnabled());
  QVERIFY(page->isVisible());
  QCOMPARE(fixture.m_tabs.currentWidget(), page.data());
  QCOMPARE(fixture.m_tabs.indexOf(page), 1);
}

void TestContentFullScreenHost::destroyingThePageRestoresTheSurvivingLayout() {
  Fixture fixture;
  QVERIFY(fixture.show());
  ContentFullScreenHost host;
  auto *parentLayout = fixture.m_page->parentWidget()->layout();
  QVERIFY(host.setFullScreen(true, fixture.m_page));
  connectExitConsumer(host);
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  delete fixture.m_page;
  QVERIFY(!host.isFullScreen());
  QVERIFY(!host.content());
  QVERIFY(parentLayout->isEnabled());
  QVERIFY(!host.exitFullScreen());
  QCoreApplication::processEvents();
  QCOMPARE(exits.count(), 0);
  QCOMPARE(fixture.m_tabs.count(), 2);
  QCOMPARE(fixture.m_tabs.currentWidget(), fixture.m_after);
  QTRY_COMPARE(fixture.m_after->geometry(), fixture.m_after->parentWidget()->contentsRect());
  fixture.m_after->setText(QStringLiteral("still usable"));
  QVERIFY(host.setFullScreen(true, fixture.m_after));
  QVERIFY(host.exitFullScreen());
  QCOMPARE(fixture.m_after->text(), QStringLiteral("still usable"));
}

void TestContentFullScreenHost::destroyingTheOwnerLeavesNoActiveHost() {
  auto *fixture = new Fixture;
  QVERIFY(fixture->show());
  ContentFullScreenHost host;
  QPointer<QWidget> page = fixture->m_page;
  QVERIFY(host.setFullScreen(true, page));
  connectExitConsumer(host);
  QSignalSpy exits(&host, &ContentFullScreenHost::exitRequested);
  delete fixture;
  QCoreApplication::processEvents();
  QVERIFY(page.isNull());
  QVERIFY(!host.isFullScreen());
  QVERIFY(!host.content());
  QVERIFY(!host.exitFullScreen());
  QCOMPARE(exits.count(), 0);
}

} // namespace tests

QTEST_MAIN(tests::TestContentFullScreenHost)
#include "test_contentfullscreenhost.moc"
