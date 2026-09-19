// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_pdfviewertoolbar.cpp
//
// The native replacement for pdf.js's built-in toolbar strip. Extracted out of
// PdfViewWindow2 precisely so it is constructible here with a bare QToolBar and
// no WebEngine profile (PdfViewWindow2 is only instantiated above Qt 6.9, and
// its setup functions are private and unreachable from a test).
//
// NOT GUILESS: menus, checked states, spin boxes and combos only mean anything
// against real widgets.

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPair>
#include <QPixmap>
#include <QSignalSpy>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>

#include <widgets/pdfviewertoolbar.h>

using vnotex::PdfViewerAdapter;
using vnotex::PdfViewerToolBar;

namespace tests {

namespace {

PdfViewerAdapter::ViewerState state(int p_page = 1, int p_pageCount = 10) {
  PdfViewerAdapter::ViewerState s;
  s.m_valid = true;
  s.m_page = p_page;
  s.m_pageCount = p_pageCount;
  s.m_scale = 1.0;
  s.m_scaleValue = QStringLiteral("auto");
  return s;
}

int checkedCount(const QList<QAction *> &p_actions) {
  int count = 0;
  for (auto *act : p_actions) {
    if (act->isChecked()) {
      ++count;
    }
  }
  return count;
}

} // namespace

class TestPdfViewerToolBar : public QObject {
  Q_OBJECT

private slots:
  void theOutlineHookRunsBetweenSidebarAndPageControls();
  void sharedMenuActionRemainsUsableAcrossViewerReadiness();
  void syncStateDoesNotEchoIntentsBack();
  void eachExclusiveGroupEndsWithExactlyOneTick();
  void aUserPickEmitsItsIntent();
  void pageStepsAreBoundedByTheDocument();
  void rotationIsRequestedAsAbsoluteDegrees();
  void anOffPresetZoomIsShownAsAPercentage();
  void theOverflowMenuSurvivesANarrowToolBar();
  void standaloneThemeRefreshUpdatesToolBarAndMenuIcons();
};

// The Outline popup belongs between the sidebar toggle and the page controls --
// both are view chrome of the same kind. It cannot be built here (it needs a
// ServiceLocator and an OutlineProvider, neither of which this component may
// hold), so install() takes a hook; without it the popup ends up AFTER the whole
// viewer toolbar, which is not the agreed layout.
void TestPdfViewerToolBar::theOutlineHookRunsBetweenSidebarAndPageControls() {
  QToolBar bar;
  PdfViewerToolBar toolBar;

  QAction *marker = nullptr;
  toolBar.install(&bar, {},
                  [&bar, &marker]() { marker = bar.addAction(QStringLiteral("Outline")); });

  const QList<QAction *> actions = bar.actions();
  const int sidebarAt = actions.indexOf(toolBar.sidebarAction());
  const int markerAt = actions.indexOf(marker);
  const int previousAt = actions.indexOf(toolBar.previousPageAction());
  const int nextAt = actions.indexOf(toolBar.nextPageAction());
  const int zoomOutAt = actions.indexOf(toolBar.zoomOutAction());

  QVERIFY(sidebarAt >= 0);
  QVERIFY(markerAt > sidebarAt);
  QVERIFY(previousAt > markerAt);
  QVERIFY(nextAt > previousAt);
  QVERIFY(zoomOutAt > nextAt);
}

// This exercises the component's readiness boundary, not ViewWindow2's menu
// construction. Caller-owned actions must survive both loading and reloading.
void TestPdfViewerToolBar::sharedMenuActionRemainsUsableAcrossViewerReadiness() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);
  toolBar.installPresentationAction(&bar);
  toolBar.installOverflowAction(&bar);
  QList<QAction *> viewerActions = {toolBar.sidebarAction(),  toolBar.previousPageAction(),
                                    toolBar.nextPageAction(), toolBar.zoomOutAction(),
                                    toolBar.zoomInAction(),   toolBar.presentationModeAction()};
  for (auto *action : toolBar.overflowMenu()->actions()) {
    if (!action->isSeparator()) {
      viewerActions.append(action);
    }
  }
  viewerActions += toolBar.cursorToolActions();
  viewerActions += toolBar.scrollModeActions();
  viewerActions += toolBar.spreadModeActions();

  auto *sharedAction = toolBar.overflowMenu()->addAction(QStringLiteral("Shared action"));
  QSignalSpy sharedSpy(sharedAction, &QAction::triggered);
  QSignalSpy pageSpy(&toolBar, &PdfViewerToolBar::pageRequested);
  QSignalSpy zoomSpy(&toolBar, &PdfViewerToolBar::zoomStepRequested);
  QSignalSpy rotationSpy(&toolBar, &PdfViewerToolBar::rotationRequested);
  QSignalSpy cursorSpy(&toolBar, &PdfViewerToolBar::cursorToolRequested);
  QSignalSpy scrollSpy(&toolBar, &PdfViewerToolBar::scrollModeRequested);
  QSignalSpy spreadSpy(&toolBar, &PdfViewerToolBar::spreadModeRequested);
  QSignalSpy sidebarSpy(&toolBar, &PdfViewerToolBar::sidebarToggleRequested);
  QSignalSpy presentationSpy(&toolBar, &PdfViewerToolBar::presentationModeRequested);
  QSignalSpy propertiesSpy(&toolBar, &PdfViewerToolBar::documentPropertiesRequested);
  const QList<QPair<QSignalSpy *, int>> expectedIntents = {
      {&pageSpy, 2},   {&zoomSpy, 2},    {&rotationSpy, 2},     {&cursorSpy, 2},    {&scrollSpy, 4},
      {&spreadSpy, 3}, {&sidebarSpy, 1}, {&presentationSpy, 1}, {&propertiesSpy, 1}};

  for (int transition = 0; transition < 3; ++transition) {
    const bool ready = transition == 1;
    // Keep an interior page even when invalid, so page bounds cannot mask a
    // readiness failure in Previous Page or Next Page.
    auto s = state(5, 10);
    s.m_valid = ready;
    toolBar.syncState(s);

    QVERIFY(toolBar.overflowAction()->isEnabled());
    QVERIFY(toolBar.overflowButton()->isEnabled());
    QVERIFY(toolBar.overflowMenu()->isEnabled());
    QVERIFY(sharedAction->isEnabled());
    sharedAction->trigger();
    QCOMPARE(sharedSpy.count(), transition + 1);

    QCOMPARE(toolBar.pageSpinBox()->isEnabled(), ready);
    QCOMPARE(toolBar.zoomComboBox()->isEnabled(), ready);
    if (!ready) {
      QVERIFY(toolBar.pageCountLabel()->text().isEmpty());
    }
    for (auto *action : viewerActions) {
      QCOMPARE(action->isEnabled(), ready);
      action->trigger();
    }
    for (const auto &intent : expectedIntents) {
      QCOMPARE(intent.first->count(), ready ? intent.second : 0);
      intent.first->clear();
    }
  }
}

// The whole point of repainting from the ADAPTER: a programmatic repaint must
// not come straight back out as a user pick and turn into a command.
void TestPdfViewerToolBar::syncStateDoesNotEchoIntentsBack() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);

  QSignalSpy pageSpy(&toolBar, &PdfViewerToolBar::pageRequested);
  QSignalSpy zoomSpy(&toolBar, &PdfViewerToolBar::zoomRequested);
  QSignalSpy scrollSpy(&toolBar, &PdfViewerToolBar::scrollModeRequested);
  QSignalSpy spreadSpy(&toolBar, &PdfViewerToolBar::spreadModeRequested);
  QSignalSpy cursorSpy(&toolBar, &PdfViewerToolBar::cursorToolRequested);
  QSignalSpy sidebarSpy(&toolBar, &PdfViewerToolBar::sidebarToggleRequested);

  auto s = state(7, 20);
  s.m_scaleValue = QStringLiteral("1.5");
  s.m_scale = 1.5;
  s.m_scrollMode = 2;
  s.m_spreadMode = 1;
  s.m_cursorTool = 1;
  s.m_sidebarOpen = true;
  toolBar.syncState(s);

  QCOMPARE(toolBar.pageSpinBox()->value(), 7);
  QCOMPARE(toolBar.pageSpinBox()->maximum(), 20);
  QCOMPARE(toolBar.zoomComboBox()->currentData().toString(), QStringLiteral("1.5"));
  QVERIFY(toolBar.sidebarAction()->isChecked());

  QCOMPARE(pageSpy.count(), 0);
  QCOMPARE(zoomSpy.count(), 0);
  QCOMPARE(scrollSpy.count(), 0);
  QCOMPARE(spreadSpy.count(), 0);
  QCOMPARE(cursorSpy.count(), 0);
  QCOMPARE(sidebarSpy.count(), 0);
}

// Fails against a QSignalBlocker wrapped around the QAction::setChecked calls:
// blocking `changed` leaves QActionGroup's bookkeeping stale, after which a
// later pick fails to clear the previous tick and TWO rows are ticked.
void TestPdfViewerToolBar::eachExclusiveGroupEndsWithExactlyOneTick() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);

  auto s = state();
  s.m_scrollMode = 1;
  s.m_spreadMode = 2;
  s.m_cursorTool = 1;
  toolBar.syncState(s);

  QCOMPARE(checkedCount(toolBar.scrollModeActions()), 1);
  QCOMPARE(checkedCount(toolBar.spreadModeActions()), 1);
  QCOMPARE(checkedCount(toolBar.cursorToolActions()), 1);
  QVERIFY(toolBar.scrollModeActions().at(1)->isChecked());

  // Now the user picks another one, and the authoritative state follows.
  toolBar.scrollModeActions().at(3)->trigger();
  auto next = s;
  next.m_scrollMode = 3;
  toolBar.syncState(next);
  QCOMPARE(checkedCount(toolBar.scrollModeActions()), 1);
  QVERIFY(toolBar.scrollModeActions().at(3)->isChecked());
}

void TestPdfViewerToolBar::aUserPickEmitsItsIntent() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);
  // Presentation Mode lives in the base class's view-mode slot, so it is
  // installed separately.
  toolBar.installPresentationAction(&bar);
  toolBar.syncState(state(5, 20));

  QSignalSpy scrollSpy(&toolBar, &PdfViewerToolBar::scrollModeRequested);
  toolBar.scrollModeActions().at(2)->trigger();
  QCOMPARE(scrollSpy.count(), 1);
  QCOMPARE(scrollSpy.at(0).at(0).toInt(), 2);

  QSignalSpy spreadSpy(&toolBar, &PdfViewerToolBar::spreadModeRequested);
  toolBar.spreadModeActions().at(1)->trigger();
  QCOMPARE(spreadSpy.at(0).at(0).toInt(), 1);

  QSignalSpy cursorSpy(&toolBar, &PdfViewerToolBar::cursorToolRequested);
  toolBar.cursorToolActions().at(1)->trigger();
  QCOMPARE(cursorSpy.at(0).at(0).toInt(), 1);

  QSignalSpy sidebarSpy(&toolBar, &PdfViewerToolBar::sidebarToggleRequested);
  toolBar.sidebarAction()->trigger();
  QCOMPARE(sidebarSpy.count(), 1);

  QSignalSpy zoomStepSpy(&toolBar, &PdfViewerToolBar::zoomStepRequested);
  toolBar.zoomInAction()->trigger();
  toolBar.zoomOutAction()->trigger();
  QCOMPARE(zoomStepSpy.count(), 2);
  QCOMPARE(zoomStepSpy.at(0).at(0).toBool(), true);
  QCOMPARE(zoomStepSpy.at(1).at(0).toBool(), false);

  QSignalSpy presentSpy(&toolBar, &PdfViewerToolBar::presentationModeRequested);
  toolBar.presentationModeAction()->trigger();
  QCOMPARE(presentSpy.count(), 1);

  QSignalSpy propsSpy(&toolBar, &PdfViewerToolBar::documentPropertiesRequested);
  toolBar.documentPropertiesAction()->trigger();
  QCOMPARE(propsSpy.count(), 1);
}

void TestPdfViewerToolBar::pageStepsAreBoundedByTheDocument() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);

  toolBar.syncState(state(1, 3));
  QVERIFY(!toolBar.previousPageAction()->isEnabled());
  QVERIFY(toolBar.nextPageAction()->isEnabled());

  QSignalSpy pageSpy(&toolBar, &PdfViewerToolBar::pageRequested);
  toolBar.previousPageAction()->trigger();
  QCOMPARE(pageSpy.count(), 0);
  toolBar.nextPageAction()->trigger();
  QCOMPARE(pageSpy.count(), 1);
  QCOMPARE(pageSpy.at(0).at(0).toInt(), 2);

  toolBar.syncState(state(3, 3));
  QVERIFY(toolBar.previousPageAction()->isEnabled());
  QVERIFY(!toolBar.nextPageAction()->isEnabled());
  toolBar.nextPageAction()->trigger();
  QCOMPARE(pageSpy.count(), 1);
}

// The adapter's command is ABSOLUTE, so the delta has to be resolved against
// the last synced state rather than duplicating "current rotation" on both
// sides.
void TestPdfViewerToolBar::rotationIsRequestedAsAbsoluteDegrees() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);

  auto s = state();
  s.m_rotation = 270;
  toolBar.syncState(s);

  QSignalSpy spy(&toolBar, &PdfViewerToolBar::rotationRequested);
  toolBar.rotateClockwiseAction()->trigger();
  QCOMPARE(spy.at(0).at(0).toInt(), 0);

  toolBar.rotateCounterClockwiseAction()->trigger();
  QCOMPARE(spy.at(1).at(0).toInt(), 180);
}

// A Ctrl+wheel zoom lands on an arbitrary factor. Leaving a stale "Automatic"
// ticked would misreport the live zoom.
void TestPdfViewerToolBar::anOffPresetZoomIsShownAsAPercentage() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);

  const int presetCount = toolBar.zoomComboBox()->count();

  auto s = state();
  s.m_scale = 1.37;
  s.m_scaleValue = QStringLiteral("1.37");
  toolBar.syncState(s);

  QCOMPARE(toolBar.zoomComboBox()->count(), presetCount + 1);
  QCOMPARE(toolBar.zoomComboBox()->currentText(), QStringLiteral("137%"));
  QCOMPARE(toolBar.zoomComboBox()->currentData().toString(), QStringLiteral("1.37"));

  // A second off-preset zoom REUSES the row rather than growing the combo.
  auto s2 = state();
  s2.m_scale = 1.62;
  s2.m_scaleValue = QStringLiteral("1.62");
  toolBar.syncState(s2);
  QCOMPARE(toolBar.zoomComboBox()->count(), presetCount + 1);
  QCOMPARE(toolBar.zoomComboBox()->currentText(), QStringLiteral("162%"));

  // ...and going back to a preset selects the preset row again.
  toolBar.syncState(state());
  QCOMPARE(toolBar.zoomComboBox()->currentData().toString(), QStringLiteral("auto"));
}

// A narrow window is the NORMAL case on a laptop with the sidebar and outline
// dock open, and it is where the overflow menu used to disappear completely.
//
// When a QToolBar runs out of room it hides the trailing items and re-offers
// them through its own extension ("»") popup, which it builds by adding the
// hidden ACTIONS to a QMenu. A QWidgetAction cannot render there, so the
// addWidget()-ed QToolButton this used to be simply vanished -- taking rotate,
// cursor, scroll mode, spread mode and document properties with
// it, while the plain actions beside it kept working.
void TestPdfViewerToolBar::theOverflowMenuSurvivesANarrowToolBar() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);
  auto *presentation = toolBar.installPresentationAction(&bar);
  toolBar.installOverflowAction(&bar);
  toolBar.syncState(state(1, 10));

  // Force the overflow.
  bar.resize(80, 40);
  bar.show();
  QVERIFY(QTest::qWaitForWindowExposed(&bar));
  QCoreApplication::processEvents();

  // Qt names its extension button; there is no public API for it.
  auto *extension = bar.findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
  QVERIFY2(extension, "the toolbar did not overflow -- the case proves nothing");
  QVERIFY(extension->menu());

  const QList<QAction *> hidden = extension->menu()->actions();
  QVERIFY2(hidden.contains(toolBar.overflowAction()),
           "the overflow entry is unreachable once the toolbar is too narrow");
  // ...and it is offered as a SUBMENU, so every verb behind it is still
  // reachable rather than being a dead row.
  QCOMPARE(hidden.at(hidden.indexOf(toolBar.overflowAction()))->menu(), toolBar.overflowMenu());
  QVERIFY(toolBar.overflowMenu()->actions().contains(toolBar.documentPropertiesAction()));
  QVERIFY(hidden.contains(presentation));
  QVERIFY(!toolBar.overflowMenu()->actions().contains(presentation));

  QSignalSpy propertiesSpy(&toolBar, &PdfViewerToolBar::documentPropertiesRequested);
  toolBar.documentPropertiesAction()->trigger();
  QCOMPARE(propertiesSpy.count(), 1);
  QSignalSpy presentationSpy(&toolBar, &PdfViewerToolBar::presentationModeRequested);
  hidden.at(hidden.indexOf(presentation))->trigger();
  QCOMPARE(presentationSpy.count(), 1);
}

void TestPdfViewerToolBar::standaloneThemeRefreshUpdatesToolBarAndMenuIcons() {
  QPixmap pixmap(8, 8);
  pixmap.fill(Qt::red);
  QIcon icon(pixmap);
  const auto icons = [&icon](const QString &) { return icon; };
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar, icons);
  toolBar.installPresentationAction(&bar, icons);
  toolBar.installOverflowAction(&bar, icons);

  pixmap.fill(Qt::blue);
  icon = QIcon(pixmap);
  toolBar.refreshIcons(icons);

  const QList<QAction *> iconActions = {
      toolBar.sidebarAction(),          toolBar.previousPageAction(),
      toolBar.nextPageAction(),         toolBar.zoomOutAction(),
      toolBar.zoomInAction(),           toolBar.overflowAction(),
      toolBar.rotateClockwiseAction(),  toolBar.rotateCounterClockwiseAction(),
      toolBar.presentationModeAction(), toolBar.documentPropertiesAction()};
  for (auto *action : iconActions) {
    QCOMPARE(action->icon().pixmap(8, 8).toImage().pixelColor(4, 4), QColor(Qt::blue));
  }
  QCOMPARE(toolBar.overflowButton()->icon().pixmap(8, 8).toImage().pixelColor(4, 4),
           QColor(Qt::blue));
}

} // namespace tests

QTEST_MAIN(tests::TestPdfViewerToolBar)
#include "test_pdfviewertoolbar.moc"
