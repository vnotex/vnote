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
#include <QApplication>
#include <QComboBox>
#include <QEnterEvent>
#include <QGraphicsEffect>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPair>
#include <QPixmap>
#include <QPointer>
#include <QSignalSpy>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
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

// Sample an empty toolbar region, not a painted child: its styled background
// must fade together with the controls over the presentation content backdrop.
struct PresentationSurface {
  QWidget m_window;
  QToolBar m_bar{&m_window};
  QWidget m_patch;
  QLineEdit m_content{&m_window};
  PdfViewerToolBar m_controls;
  QMenu *m_outlineMenu = nullptr;
  const QColor m_parentColor{20, 40, 60};
  const QColor m_toolBarColor{220, 140, 80};

  PresentationSurface() {
    auto palette = m_window.palette();
    palette.setColor(QPalette::Window, m_parentColor);
    m_window.setPalette(palette);
    m_window.setAutoFillBackground(true);

    m_bar.setStyleSheet(QStringLiteral("QToolBar { background-color: %1; border: none; }")
                            .arg(m_toolBarColor.name()));
    m_patch.setFixedSize(80, 28);
    m_bar.addWidget(&m_patch);
    m_bar.setMovable(false);

    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::black);
    const auto icons = [pixmap](const QString &) { return QIcon(pixmap); };
    m_controls.install(&m_bar, icons, [this, &icons]() {
      auto *action = m_bar.addAction(icons(QString()), QStringLiteral("Outline"));
      m_outlineMenu = new QMenu(&m_bar);
      m_outlineMenu->addAction(QStringLiteral("Alpha"));
      action->setMenu(m_outlineMenu);
      auto *button = qobject_cast<QToolButton *>(m_bar.widgetForAction(action));
      button->setPopupMode(QToolButton::InstantPopup);
    });
    m_controls.installPresentationAction(&m_bar, icons);
    m_controls.installOverflowAction(&m_bar, icons);
    m_controls.syncState(state());

    auto *layout = new QVBoxLayout(&m_window);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(&m_bar);
    layout->addWidget(&m_content, 1);
    m_content.setText(QStringLiteral("PDF content"));
  }

  void show() {
    m_window.resize(1000, 300);
    m_window.show();
    m_window.raise();
    m_window.activateWindow();
  }

  void movePointer(QWidget *p_target) {
    const auto global = p_target->mapToGlobal(p_target->rect().center());
    QTest::mouseMove(p_target, p_target->rect().center());
    // Deliver the Qt hover boundary too: a locked Windows desktop can discard
    // the native cursor movement even though its windows still render normally.
    if (p_target->window() == &m_window && m_bar.rect().contains(m_bar.mapFromGlobal(global))) {
      QEnterEvent enter(m_bar.mapFromGlobal(global), m_window.mapFromGlobal(global), global);
      QCoreApplication::sendEvent(&m_bar, &enter);
    } else {
      QEvent leave(QEvent::Leave);
      QCoreApplication::sendEvent(&m_bar, &leave);
    }
  }

  QColor sampleColor() {
    const auto image = m_window.grab().toImage();
    const auto point = m_patch.mapTo(&m_window, m_patch.rect().center());
    const auto ratio = image.devicePixelRatio();
    return image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
  }

  QColor inactiveColor() const {
    return QColor(qRound(0.1 * m_toolBarColor.red() + 0.9 * m_parentColor.red()),
                  qRound(0.1 * m_toolBarColor.green() + 0.9 * m_parentColor.green()),
                  qRound(0.1 * m_toolBarColor.blue() + 0.9 * m_parentColor.blue()));
  }
};

bool colorNear(const QColor &p_actual, const QColor &p_expected) {
  return qAbs(p_actual.red() - p_expected.red()) <= 2 &&
         qAbs(p_actual.green() - p_expected.green()) <= 2 &&
         qAbs(p_actual.blue() - p_expected.blue()) <= 2;
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
  void presentationToggleFollowsConfirmedState();
  void presentationCanExitWhileViewerReloads();
  void presentationOpacityTracksHoverFocusAndPopups();
  void presentationOpacityTracksWindowActivation();
  void presentationTrackingEndsWithEitherOwner();
  void standaloneThemeRefreshUpdatesToolBarAndMenuIcons();
};

// The hook must keep Outline and Find together before page navigation. These
// markers test the component's insertion slot, not PdfViewWindow2's wiring;
// the real Outline popup requires a ServiceLocator and an OutlineProvider.
void TestPdfViewerToolBar::theOutlineHookRunsBetweenSidebarAndPageControls() {
  QToolBar bar;
  PdfViewerToolBar toolBar;

  QAction *marker = nullptr;
  QAction *findMarker = nullptr;
  toolBar.install(&bar, {}, [&bar, &marker, &findMarker]() {
    marker = bar.addAction(QStringLiteral("Outline"));
    findMarker = bar.addAction(QStringLiteral("Find And Replace"));
  });

  const QList<QAction *> actions = bar.actions();
  const int sidebarAt = actions.indexOf(toolBar.sidebarAction());
  const int markerAt = actions.indexOf(marker);
  const int findAt = actions.indexOf(findMarker);
  const int previousAt = actions.indexOf(toolBar.previousPageAction());
  const int nextAt = actions.indexOf(toolBar.nextPageAction());
  const int zoomOutAt = actions.indexOf(toolBar.zoomOutAction());

  QVERIFY(sidebarAt >= 0);
  QVERIFY(markerAt > sidebarAt);
  QCOMPARE(findAt, markerAt + 1);
  QVERIFY(actions.at(findAt + 1)->isSeparator());
  QCOMPARE(previousAt, findAt + 2);
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
  connect(&toolBar, &PdfViewerToolBar::presentationModeRequested, &toolBar,
          [&toolBar]() { toolBar.setPresentationMode(false); });
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
  bar.activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(&bar));
  toolBar.setPresentationMode(true);
  auto reloading = state();
  reloading.m_valid = false;
  toolBar.syncState(reloading);
  QVERIFY(presentation->isChecked());
  QVERIFY(presentation->isEnabled());

  QSignalSpy presentationSpy(&toolBar, &PdfViewerToolBar::presentationModeRequested);
  connect(&toolBar, &PdfViewerToolBar::presentationModeRequested, &toolBar,
          [&toolBar]() { toolBar.setPresentationMode(false); });
  auto *menu = extension->menu();
  menu->popup(bar.mapToGlobal(QPoint(0, bar.height())));
  QTRY_VERIFY(menu->isVisible());
  QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                    menu->actionGeometry(presentation).center());
  QTRY_COMPARE(presentationSpy.count(), 1);
  QVERIFY(!presentation->isChecked());
  QVERIFY(!presentation->isEnabled());
  QVERIFY(!bar.graphicsEffect());
}

void TestPdfViewerToolBar::presentationToggleFollowsConfirmedState() {
  PresentationSurface surface;
  auto &controls = surface.m_controls;
  auto *action = controls.presentationModeAction();
  auto *button = qobject_cast<QToolButton *>(surface.m_bar.widgetForAction(action));
  QVERIFY(button);
  QVERIFY(action->isCheckable());
  bool presenting = false;
  bool acceptEntry = true;
  connect(&controls, &PdfViewerToolBar::presentationModeRequested, &controls, [&]() {
    if (presenting || acceptEntry) {
      presenting = !presenting;
    }
    controls.setPresentationMode(presenting);
  });
  QSignalSpy requests(&controls, &PdfViewerToolBar::presentationModeRequested);
  surface.show();
  QVERIFY(QTest::qWaitForWindowActive(&surface.m_window));
  QVERIFY(button->isVisible());

  QTest::mouseClick(button, Qt::LeftButton);
  QCOMPARE(requests.count(), 1);
  QVERIFY(presenting);
  QVERIFY(action->isChecked());
  QTest::mouseClick(button, Qt::LeftButton);
  QCOMPARE(requests.count(), 2);
  QVERIFY(!presenting);
  QVERIFY(!action->isChecked());

  acceptEntry = false;
  QTest::mouseClick(button, Qt::LeftButton);
  QCOMPARE(requests.count(), 3);
  QVERIFY(!presenting);
  QVERIFY(!action->isChecked());
  QVERIFY(!button->isChecked());

  controls.setPresentationMode(true);
  QVERIFY(action->isChecked());
  QVERIFY(button->isChecked());
  controls.setPresentationMode(false);
  QVERIFY(!action->isChecked());
  QVERIFY(!button->isChecked());
  QCOMPARE(requests.count(), 3);
}

void TestPdfViewerToolBar::presentationCanExitWhileViewerReloads() {
  PresentationSurface surface;
  auto &controls = surface.m_controls;
  auto *action = controls.presentationModeAction();
  auto *button = qobject_cast<QToolButton *>(surface.m_bar.widgetForAction(action));
  QVERIFY(button);
  auto reloading = state();
  reloading.m_valid = false;
  bool presenting = false;
  bool exitStayedEnabled = false;
  connect(&controls, &PdfViewerToolBar::presentationModeRequested, &controls, [&]() {
    if (presenting) {
      // The click has already unchecked QAction. It is not confirmed state.
      controls.syncState(reloading);
      exitStayedEnabled = action->isEnabled();
    }
    presenting = !presenting;
    controls.setPresentationMode(presenting);
  });
  QSignalSpy requests(&controls, &PdfViewerToolBar::presentationModeRequested);
  surface.show();
  QVERIFY(QTest::qWaitForWindowActive(&surface.m_window));
  QTest::mouseClick(button, Qt::LeftButton);
  QVERIFY(presenting);

  controls.syncState(reloading);
  QVERIFY(!controls.pageSpinBox()->isEnabled());
  QVERIFY(!controls.zoomComboBox()->isEnabled());
  QVERIFY(action->isChecked());
  QVERIFY(action->isEnabled());
  QTest::mouseClick(button, Qt::LeftButton);
  QCOMPARE(requests.count(), 2);
  QVERIFY(exitStayedEnabled);
  QVERIFY(!presenting);
  QVERIFY(!action->isChecked());
  QVERIFY(!action->isEnabled());
  QTest::mouseClick(button, Qt::LeftButton);
  QCOMPARE(requests.count(), 2);
}

void TestPdfViewerToolBar::presentationOpacityTracksHoverFocusAndPopups() {
  PresentationSurface surface;
  surface.show();
  QVERIFY(QTest::qWaitForWindowActive(&surface.m_window));
  QTest::mouseClick(&surface.m_content, Qt::LeftButton);
  surface.movePointer(&surface.m_content);
  surface.m_controls.setPresentationMode(true);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.inactiveColor()));
  QVERIFY(!surface.m_content.graphicsEffect());
  QVERIFY(!surface.m_window.graphicsEffect());

  surface.movePointer(&surface.m_patch);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));
  auto *page = surface.m_controls.pageSpinBox();
  QTest::mouseClick(page, Qt::LeftButton);
  QVERIFY(surface.m_bar.isAncestorOf(QApplication::focusWidget()));
  surface.movePointer(&surface.m_content);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));
  QTest::mouseClick(&surface.m_content, Qt::LeftButton);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.inactiveColor()));

  // Outline is caller-owned rather than built by PdfViewerToolBar itself.
  const auto popupPoint = surface.m_content.mapToGlobal(QPoint(20, 20));
  surface.m_outlineMenu->popup(popupPoint);
  QTRY_VERIFY(surface.m_outlineMenu->isVisible());
  surface.movePointer(surface.m_outlineMenu);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));
  QTest::keyClick(surface.m_outlineMenu, Qt::Key_Escape);
  QTRY_VERIFY(!surface.m_outlineMenu->isVisible());
  surface.movePointer(&surface.m_content);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.inactiveColor()));

  auto *menu = surface.m_controls.overflowMenu();
  auto *submenu = qobject_cast<QMenu *>(surface.m_controls.scrollModeActions().first()->parent());
  QVERIFY(submenu);
  menu->popup(popupPoint);
  QTRY_VERIFY(menu->isVisible());
  menu->setActiveAction(submenu->menuAction());
  QTest::keyClick(menu, Qt::Key_Right);
  QTRY_VERIFY(submenu->isVisible());
  surface.movePointer(submenu);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));
  QTest::keyClick(submenu, Qt::Key_Escape);
  QTRY_VERIFY(!submenu->isVisible());
  QVERIFY(menu->isVisible());
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));
  QTest::keyClick(menu, Qt::Key_Escape);
  surface.movePointer(&surface.m_content);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.inactiveColor()));

  auto *combo = surface.m_controls.zoomComboBox();
  QTest::mouseClick(combo, Qt::LeftButton, Qt::NoModifier,
                    QPoint(combo->width() - 8, combo->height() / 2));
  QTRY_VERIFY(QApplication::activePopupWidget());
  auto *popup = QApplication::activePopupWidget();
  surface.movePointer(popup);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));
  QTest::keyClick(popup, Qt::Key_Escape);
  QTRY_VERIFY(!QApplication::activePopupWidget());
  QTest::mouseClick(&surface.m_content, Qt::LeftButton);
  surface.movePointer(&surface.m_content);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.inactiveColor()));

  surface.m_window.resize(220, 300);
  auto *extension = surface.m_bar.findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
  QVERIFY(extension);
  QTRY_VERIFY(extension->isVisible());
  QVERIFY(surface.m_patch.isVisible());
  QVERIFY(extension->menu());
  menu = extension->menu();
  menu->popup(surface.m_content.mapToGlobal(QPoint(20, 20)));
  QTRY_VERIFY(menu->isVisible());
  surface.movePointer(menu);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));
  QTest::keyClick(menu, Qt::Key_Escape);
  surface.movePointer(&surface.m_content);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.inactiveColor()));

  surface.m_controls.setPresentationMode(false);
  QVERIFY(!surface.m_bar.graphicsEffect());
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));
}

void TestPdfViewerToolBar::presentationOpacityTracksWindowActivation() {
  PresentationSurface surface;
  surface.show();
  QVERIFY(QTest::qWaitForWindowActive(&surface.m_window));
  QTest::mouseClick(surface.m_controls.pageSpinBox(), Qt::LeftButton);
  surface.movePointer(&surface.m_content);
  surface.m_controls.setPresentationMode(true);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));

  QWidget unrelated;
  unrelated.resize(200, 100);
  unrelated.show();
  unrelated.raise();
  unrelated.activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(&unrelated));
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.inactiveColor()));

  unrelated.hide();
  surface.m_window.raise();
  surface.m_window.activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(&surface.m_window));
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));
  surface.m_controls.setPresentationMode(false);
  QTest::mouseClick(&surface.m_content, Qt::LeftButton);
  QTRY_VERIFY(colorNear(surface.sampleColor(), surface.m_toolBarColor));
}

void TestPdfViewerToolBar::presentationTrackingEndsWithEitherOwner() {
  QToolBar bar;
  auto *controls = new PdfViewerToolBar;
  controls->install(&bar);
  controls->installPresentationAction(&bar);
  controls->setPresentationMode(true);
  QVERIFY(bar.graphicsEffect());
  bar.show();
  delete controls;
  QVERIFY(!bar.graphicsEffect());

  PdfViewerToolBar survivingControls;
  auto *shortLivedBar = new QToolBar;
  survivingControls.install(shortLivedBar);
  survivingControls.installPresentationAction(shortLivedBar);
  survivingControls.setPresentationMode(true);
  QPointer<QObject> effect = shortLivedBar->graphicsEffect();
  QVERIFY(effect);
  shortLivedBar->show();
  delete shortLivedBar;
  QVERIFY(!effect);
  // Flush any already queued hover/focus evaluation after the toolbar is gone.
  QCoreApplication::processEvents();
  survivingControls.setPresentationMode(false);
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
