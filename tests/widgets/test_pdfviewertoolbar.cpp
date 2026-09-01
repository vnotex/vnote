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
#include <QSignalSpy>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include <QtTest>

#include <widgets/pdfviewertoolbar.h>
#include <widgets/propertydefs.h>

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
  void installBuildsEveryControl();
  void theOutlineHookRunsBetweenSidebarAndPageControls();
  void controlsAreDeadUntilTheFirstAcceptedState();
  void syncStateDoesNotEchoIntentsBack();
  void eachExclusiveGroupEndsWithExactlyOneTick();
  void aUserPickEmitsItsIntent();
  void pageStepsAreBoundedByTheDocument();
  void rotationIsRequestedAsAbsoluteDegrees();
  void anOffPresetZoomIsShownAsAPercentage();
  void theOverflowButtonHidesItsMenuIndicator();
  void theOverflowMenuSurvivesANarrowToolBar();
  void presentationModeSitsOnTheToolBarNotInTheOverflowMenu();
  void theOverflowButtonIsTheLastThingOnTheToolBar();
  void everyIconBearingActionCarriesItsIconName();
};

void TestPdfViewerToolBar::installBuildsEveryControl() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);

  QVERIFY(toolBar.sidebarAction());
  QVERIFY(toolBar.sidebarAction()->isCheckable());
  QVERIFY(toolBar.previousPageAction());
  QVERIFY(toolBar.nextPageAction());
  QVERIFY(toolBar.pageSpinBox());
  QVERIFY(toolBar.pageCountLabel());
  QVERIFY(toolBar.zoomOutAction());
  QVERIFY(toolBar.zoomInAction());
  QVERIFY(toolBar.zoomComboBox());
  // The overflow MENU is built by install(); the toolbar entry that opens it
  // is placed later, by installOverflowAction().
  QVERIFY(toolBar.overflowMenu());
  QVERIFY(!toolBar.overflowButton());
  QVERIFY(!toolBar.overflowAction());
  QVERIFY(toolBar.rotateClockwiseAction());
  QVERIFY(toolBar.rotateCounterClockwiseAction());
  QVERIFY(toolBar.documentPropertiesAction());

  // pdf.js's own mode vocabularies.
  QCOMPARE(toolBar.cursorToolActions().size(), 2);
  QCOMPARE(toolBar.scrollModeActions().size(), 4);
  QCOMPARE(toolBar.spreadModeActions().size(), 3);

  // The zoom combo carries the four presets plus the percentage rows, and the
  // data strings are the WIRE vocabulary shared with pdfviewercore.js.
  QCOMPARE(toolBar.zoomComboBox()->itemData(0).toString(), QStringLiteral("auto"));
  QCOMPARE(toolBar.zoomComboBox()->itemData(1).toString(), QStringLiteral("page-actual"));
  QCOMPARE(toolBar.zoomComboBox()->itemData(2).toString(), QStringLiteral("page-fit"));
  QCOMPARE(toolBar.zoomComboBox()->itemData(3).toString(), QStringLiteral("page-width"));
  QVERIFY(toolBar.zoomComboBox()->findData(QStringLiteral("1")) >= 0);
}

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
  QVERIFY(marker);

  const QList<QAction *> actions = bar.actions();
  const int sidebarAt = actions.indexOf(toolBar.sidebarAction());
  const int markerAt = actions.indexOf(marker);
  const int previousAt = actions.indexOf(toolBar.previousPageAction());
  const int nextAt = actions.indexOf(toolBar.nextPageAction());
  const int zoomOutAt = actions.indexOf(toolBar.zoomOutAction());

  QVERIFY(sidebarAt >= 0);
  QCOMPARE(markerAt, sidebarAt + 1);
  // A separator sits between the hook and the page controls.
  QVERIFY(actions.at(markerAt + 1)->isSeparator());
  QCOMPARE(previousAt, markerAt + 2);
  QVERIFY(nextAt > previousAt);
  QVERIFY(zoomOutAt > nextAt);
  // Zoom is the last thing install() places; the base class's Readable Width,
  // Presentation Mode and Find And Replace come next, and the overflow button
  // is appended after all of them by installOverflowAction().
  QCOMPARE(zoomOutAt, actions.size() - 3);

  // The hook is optional: nothing else may depend on it.
  QToolBar bare;
  PdfViewerToolBar plain;
  plain.install(&bare);
  QVERIFY(bare.actions().indexOf(plain.sidebarAction()) >= 0);
}

// A blank window must have no live controls, rather than controls that silently
// do nothing.
void TestPdfViewerToolBar::controlsAreDeadUntilTheFirstAcceptedState() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);
  toolBar.installOverflowAction(&bar);

  QVERIFY(!toolBar.sidebarAction()->isEnabled());
  QVERIFY(!toolBar.pageSpinBox()->isEnabled());
  QVERIFY(!toolBar.zoomComboBox()->isEnabled());
  QVERIFY(!toolBar.overflowButton()->isEnabled());
  // Every MENU entry individually: QAction::trigger() consults only the
  // action's own enabled state, so disabling the menu alone leaves each row
  // invokable.
  QVERIFY(!toolBar.rotateClockwiseAction()->isEnabled());
  QVERIFY(!toolBar.scrollModeActions().at(0)->isEnabled());

  toolBar.syncState(state(1, 10));
  QVERIFY(toolBar.sidebarAction()->isEnabled());
  QVERIFY(toolBar.pageSpinBox()->isEnabled());
  QVERIFY(toolBar.zoomComboBox()->isEnabled());
  QVERIFY(toolBar.overflowButton()->isEnabled());
  QVERIFY(toolBar.rotateClockwiseAction()->isEnabled());
  QVERIFY(toolBar.scrollModeActions().at(0)->isEnabled());

  // A reload drives it back to not-valid.
  toolBar.syncState(PdfViewerAdapter::ViewerState());
  QVERIFY(!toolBar.pageSpinBox()->isEnabled());
  QVERIFY(toolBar.pageCountLabel()->text().isEmpty());
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

// The overflow button is an InstantPopup ICON button (Outline / Tag /
// Attachment shape), so the built-in dropdown arrow is redundant chrome. This
// is the OPPOSITE of PdfAnnotationToolBar's MenuButtonPopup buttons, where the
// indicator is the entire affordance and the property must stay unset.
void TestPdfViewerToolBar::theOverflowButtonHidesItsMenuIndicator() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);
  toolBar.installOverflowAction(&bar);

  QCOMPARE(toolBar.overflowButton()->popupMode(), QToolButton::InstantPopup);
  QCOMPARE(toolBar.overflowButton()
               ->property(vnotex::PropertyDefs::c_toolButtonWithoutMenuIndicator)
               .toBool(),
           true);
}

// A narrow window is the NORMAL case on a laptop with the sidebar and outline
// dock open, and it is where the overflow menu used to disappear completely.
//
// When a QToolBar runs out of room it hides the trailing items and re-offers
// them through its own extension ("»") popup, which it builds by adding the
// hidden ACTIONS to a QMenu. A QWidgetAction cannot render there, so the
// addWidget()-ed QToolButton this used to be simply vanished -- taking rotate,
// cursor, scroll mode, spread mode, presentation and document properties with
// it, while the plain actions beside it kept working.
void TestPdfViewerToolBar::theOverflowMenuSurvivesANarrowToolBar() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);
  toolBar.installOverflowAction(&bar);
  toolBar.syncState(state(1, 10));

  // The property that makes both surfaces work from one declaration:
  // QToolButton::menu() falls back to defaultAction()->menu() on the toolbar,
  // and QMenu renders an action-with-a-menu as a submenu in the extension
  // popup.
  QCOMPARE(toolBar.overflowAction()->menu(), toolBar.overflowMenu());
  QVERIFY2(!qobject_cast<QWidgetAction *>(toolBar.overflowAction()),
           "the overflow entry must be a plain action; a QWidgetAction cannot "
           "render in the toolbar's extension popup");

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
}

// Presentation Mode belongs beside Readable Width, not buried in the overflow
// menu: both change how the content is PRESENTED, and that is where a reader
// looks for them. A mode whose own toolbar disappears once it is on should at
// least have a visible way IN.
//
// It cannot be installed by install(), because the slot it occupies is owned by
// the base class -- ViewWindow2 adds Readable Width and Find And Replace after
// addAdditionalRightToolBarActions() has already returned.
void TestPdfViewerToolBar::presentationModeSitsOnTheToolBarNotInTheOverflowMenu() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);

  // install() alone must not create it, or it would land in the wrong region.
  QVERIFY(!toolBar.presentationModeAction());
  QVERIFY(!toolBar.overflowMenu()->actions().contains(toolBar.presentationModeAction()));

  // Stand in for the base class: Readable Width, then the hook, then Find.
  auto *readableWidth = bar.addAction(QStringLiteral("Readable Width"));
  auto *presentation = toolBar.installPresentationAction(&bar);
  auto *find = bar.addAction(QStringLiteral("Find and Replace"));

  QVERIFY(presentation);
  QCOMPARE(toolBar.presentationModeAction(), presentation);
  QVERIFY2(!toolBar.overflowMenu()->actions().contains(presentation),
           "Presentation Mode must not be offered in two places at once");

  // Directly after Readable Width, before Find.
  const QList<QAction *> actions = bar.actions();
  QCOMPARE(actions.indexOf(presentation), actions.indexOf(readableWidth) + 1);
  QCOMPARE(actions.indexOf(find), actions.indexOf(presentation) + 1);

  // It is a plain action, so it survives into the extension popup when the
  // toolbar is too narrow -- the same rule the overflow entry follows.
  QVERIFY(!qobject_cast<QWidgetAction *>(presentation));
  QVERIFY(!presentation->property("iconName").toString().isEmpty());

  // Installed after install()'s enable sweep, so it must pick up the current
  // state rather than defaulting to enabled on a blank window.
  QVERIFY(!presentation->isEnabled());
  toolBar.syncState(state(1, 10));
  QVERIFY(presentation->isEnabled());

  QSignalSpy spy(&toolBar, &PdfViewerToolBar::presentationModeRequested);
  presentation->trigger();
  QCOMPARE(spy.count(), 1);
}

// The overflow button is the toolbar's catch-all, so it belongs at the very END
// -- past everything the base class appends. install() cannot put it there:
// Readable Width, Presentation Mode and Find And Replace are all added after
// addAdditionalRightToolBarActions() has returned, so only
// PdfViewWindow2::setupToolBar() can reach the position.
void TestPdfViewerToolBar::theOverflowButtonIsTheLastThingOnTheToolBar() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);

  // Stand in for everything ViewWindow2::addRightCommonToolBarActions() appends.
  bar.addAction(QStringLiteral("Readable Width"));
  toolBar.installPresentationAction(&bar);
  auto *find = bar.addAction(QStringLiteral("Find and Replace"));

  auto *overflow = toolBar.installOverflowAction(&bar);
  QVERIFY(overflow);
  QCOMPARE(toolBar.overflowAction(), overflow);

  const QList<QAction *> actions = bar.actions();
  QCOMPARE(actions.last(), overflow);
  // Directly after Find, with NO separator: being last already sets it apart,
  // and a rule against the window edge is clutter.
  QCOMPARE(actions.indexOf(overflow), actions.indexOf(find) + 1);

  QCOMPARE(bar.widgetForAction(overflow), static_cast<QWidget *>(toolBar.overflowButton()));
  QCOMPARE(overflow->menu(), toolBar.overflowMenu());

  // install()'s enable sweep already ran, so a late arrival must match it
  // rather than defaulting to enabled on a blank window.
  QVERIFY(!overflow->isEnabled());
  QVERIFY(!toolBar.overflowButton()->isEnabled());
  toolBar.syncState(state(1, 10));
  QVERIFY(overflow->isEnabled());
  QVERIFY(toolBar.overflowButton()->isEnabled());
}

// Theme refresh is NOT automatic: ViewWindowToolBarHelper2::refreshToolBarIcons()
// regenerates only actions carrying a non-empty `iconName`. An action without
// one silently keeps the previous theme's tint forever.
void TestPdfViewerToolBar::everyIconBearingActionCarriesItsIconName() {
  QToolBar bar;
  PdfViewerToolBar toolBar;
  toolBar.install(&bar);
  toolBar.installPresentationAction(&bar);
  toolBar.installOverflowAction(&bar);

  const QList<QAction *> iconActions = {toolBar.sidebarAction(),
                                        toolBar.previousPageAction(),
                                        toolBar.nextPageAction(),
                                        toolBar.zoomOutAction(),
                                        toolBar.zoomInAction(),
                                        toolBar.rotateClockwiseAction(),
                                        toolBar.rotateCounterClockwiseAction(),
                                        toolBar.presentationModeAction(),
                                        toolBar.documentPropertiesAction()};
  for (auto *act : iconActions) {
    QVERIFY2(!act->property("iconName").toString().isEmpty(),
             qPrintable(QStringLiteral("no iconName on %1").arg(act->text())));
  }

  // refreshIcons() reaches the menu entries and the overflow BUTTON, neither of
  // which refreshToolBarIcons() can touch.
  QPixmap pixmap(8, 8);
  pixmap.fill(Qt::red);
  const QIcon marker(pixmap);
  toolBar.refreshIcons([&marker](const QString &) { return marker; });
  QVERIFY(!toolBar.rotateClockwiseAction()->icon().isNull());
  QVERIFY(!toolBar.overflowButton()->icon().isNull());
}

} // namespace tests

QTEST_MAIN(tests::TestPdfViewerToolBar)
#include "test_pdfviewertoolbar.moc"
