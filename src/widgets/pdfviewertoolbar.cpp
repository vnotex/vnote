#include "pdfviewertoolbar.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QtMath>

#include "propertydefs.h"

using namespace vnotex;

namespace {

// Numeric zoom presets, in the order pdf.js's own #scaleSelect lists them. The
// data string is the wire vocabulary shared with pdfviewercore.js and
// PdfViewerAdapter -- one spelling end to end, so nothing has to translate.
struct ZoomPreset {
  const char *m_value;
  int m_percent;
};

const ZoomPreset c_zoomPresets[] = {
    {"0.5", 50},  {"0.75", 75}, {"1", 100}, {"1.25", 125},
    {"1.5", 150}, {"2", 200},   {"3", 300}, {"4", 400},
};

} // namespace

PdfViewerToolBar::PdfViewerToolBar(QObject *p_parent) : QObject(p_parent) {}

QAction *PdfViewerToolBar::addIconAction(QToolBar *p_toolBar, const QString &p_iconName,
                                         const QString &p_text, const IconProvider &p_icons) {
  auto *act = p_toolBar->addAction(p_icons ? p_icons(p_iconName) : QIcon(), p_text);
  // The property is what makes ViewWindow2::handleThemeChanged() ->
  // ViewWindowToolBarHelper2::refreshToolBarIcons() re-tint this action: that
  // function regenerates ONLY actions carrying a non-empty `iconName`.
  act->setProperty("iconName", p_iconName);
  return act;
}

void PdfViewerToolBar::install(QToolBar *p_toolBar, const IconProvider &p_icons,
                               const std::function<void()> &p_afterSidebar) {
  Q_ASSERT(p_toolBar);
  Q_ASSERT(!m_sidebarAction);

  // 1. Sidebar toggle. pdf.js keeps the pane itself (thumbnails / outline /
  //    attachments / layers) and its OWN view-selector row; only the top strip
  //    is hidden, so this has to remain reachable.
  m_sidebarAction =
      addIconAction(p_toolBar, QStringLiteral("sidebar_editor.svg"), tr("Toggle Sidebar"), p_icons);
  m_sidebarAction->setCheckable(true);
  connect(m_sidebarAction, &QAction::triggered, this, [this]() { emit sidebarToggleRequested(); });

  // 2. The Outline popup goes HERE, between the sidebar toggle and the page
  //    controls -- it is view chrome of the same kind. The hook exists because
  //    the popup needs PdfViewWindow2's ServiceLocator and outline provider,
  //    neither of which this component may hold (that is the whole reason it is
  //    constructible in a test with a bare QToolBar).
  if (p_afterSidebar) {
    p_afterSidebar();
  }

  // 3. Page navigation.
  m_separators.append(p_toolBar->addSeparator());

  m_previousPageAction = addIconAction(p_toolBar, QStringLiteral("page_previous_editor.svg"),
                                       tr("Previous Page"), p_icons);
  connect(m_previousPageAction, &QAction::triggered, this, [this]() {
    if (m_state.m_page > 1) {
      emit pageRequested(m_state.m_page - 1);
    }
  });

  m_pageSpinBox = new QSpinBox();
  m_pageSpinBox->setRange(1, 1);
  m_pageSpinBox->setValue(1);
  m_pageSpinBox->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  m_pageSpinBox->setToolTip(tr("Page"));
  // Not on valueChanged: the spin box emits that per keystroke and per arrow
  // press, and each one would be a full re-layout of the document.
  connect(m_pageSpinBox, &QSpinBox::editingFinished, this,
          [this]() { emit pageRequested(m_pageSpinBox->value()); });
  m_pageSpinBoxAction = p_toolBar->addWidget(m_pageSpinBox);

  m_pageCountLabel = new QLabel();
  m_pageCountLabel->setProperty(PropertyDefs::c_mutedText, true);
  m_pageCountLabelAction = p_toolBar->addWidget(m_pageCountLabel);

  m_nextPageAction =
      addIconAction(p_toolBar, QStringLiteral("page_next_editor.svg"), tr("Next Page"), p_icons);
  connect(m_nextPageAction, &QAction::triggered, this, [this]() {
    if (m_state.m_pageCount > 0 && m_state.m_page < m_state.m_pageCount) {
      emit pageRequested(m_state.m_page + 1);
    }
  });

  // 4. Zoom.
  m_separators.append(p_toolBar->addSeparator());

  m_zoomOutAction =
      addIconAction(p_toolBar, QStringLiteral("zoom_out_editor.svg"), tr("Zoom Out"), p_icons);
  connect(m_zoomOutAction, &QAction::triggered, this, [this]() { emit zoomStepRequested(false); });

  m_zoomComboBox = new QComboBox();
  m_zoomComboBox->addItem(tr("Automatic"), QStringLiteral("auto"));
  m_zoomComboBox->addItem(tr("Actual Size"), QStringLiteral("page-actual"));
  m_zoomComboBox->addItem(tr("Page Fit"), QStringLiteral("page-fit"));
  m_zoomComboBox->addItem(tr("Page Width"), QStringLiteral("page-width"));
  for (const auto &preset : c_zoomPresets) {
    m_zoomComboBox->addItem(QStringLiteral("%1%").arg(preset.m_percent),
                            QString::fromLatin1(preset.m_value));
  }
  connect(m_zoomComboBox, QOverload<int>::of(&QComboBox::activated), this, [this](int p_index) {
    // `activated` (not `currentIndexChanged`): it fires only for a USER pick,
    // which makes the QSignalBlocker in syncState a belt-and-braces measure
    // rather than the only guard.
    const auto value = m_zoomComboBox->itemData(p_index).toString();
    if (!value.isEmpty()) {
      emit zoomRequested(value);
    }
  });
  m_zoomComboBoxAction = p_toolBar->addWidget(m_zoomComboBox);

  m_zoomInAction =
      addIconAction(p_toolBar, QStringLiteral("zoom_in_editor.svg"), tr("Zoom In"), p_icons);
  connect(m_zoomInAction, &QAction::triggered, this, [this]() { emit zoomStepRequested(true); });

  // 5. The overflow MENU is built now; the toolbar entry that opens it is
  //    placed later, by installOverflowAction(), because it belongs at the very
  //    end of the toolbar -- after everything the base class appends.
  buildOverflowMenu(p_toolBar, p_icons);

  // Nothing is live until the first accepted viewer state.
  setControlsEnabled(false);
}

QMenu *PdfViewerToolBar::addModeSubmenu(const QString &p_title, const QList<QString> &p_labels,
                                        QList<QAction *> &p_actions, QActionGroup *&p_group,
                                        void (PdfViewerToolBar::*p_signal)(int)) {
  auto *menu = m_overflowMenu->addMenu(p_title);
  // Exclusive: these are radio modes -- unlike the annotation tool group, which
  // is non-exclusive precisely so clicking the armed tool disarms it. There is
  // no "no scroll mode".
  p_group = new QActionGroup(menu);
  p_group->setExclusive(true);

  for (int i = 0; i < p_labels.size(); ++i) {
    auto *act = menu->addAction(p_labels.at(i));
    act->setCheckable(true);
    act->setData(i);
    p_group->addAction(act);
    connect(act, &QAction::triggered, this, [this, i, p_signal]() { (this->*p_signal)(i); });
    p_actions.append(act);
  }
  return menu;
}

void PdfViewerToolBar::buildOverflowMenu(QToolBar *p_toolBar, const IconProvider &p_icons) {
  // Only the MENU. The toolbar entry that opens it is added by
  // installOverflowAction(), which runs after the base class has finished the
  // toolbar so the button can sit at the very end.
  m_overflowMenu = new QMenu(p_toolBar);

  m_rotateCwAction = m_overflowMenu->addAction(
      p_icons ? p_icons(QStringLiteral("rotate_cw_editor.svg")) : QIcon(), tr("Rotate Clockwise"));
  m_rotateCwAction->setProperty("iconName", QStringLiteral("rotate_cw_editor.svg"));
  connect(m_rotateCwAction, &QAction::triggered, this,
          [this]() { emit rotationRequested((m_state.m_rotation + 90) % 360); });

  m_rotateCcwAction = m_overflowMenu->addAction(
      p_icons ? p_icons(QStringLiteral("rotate_ccw_editor.svg")) : QIcon(),
      tr("Rotate Counterclockwise"));
  m_rotateCcwAction->setProperty("iconName", QStringLiteral("rotate_ccw_editor.svg"));
  connect(m_rotateCcwAction, &QAction::triggered, this,
          [this]() { emit rotationRequested((m_state.m_rotation + 270) % 360); });

  addModeSubmenu(tr("Cursor"), {tr("Select Text"), tr("Hand Tool")}, m_cursorActions, m_cursorGroup,
                 &PdfViewerToolBar::cursorToolRequested);
  addModeSubmenu(tr("Scroll Mode"), {tr("Vertical"), tr("Horizontal"), tr("Wrapped"), tr("Page")},
                 m_scrollActions, m_scrollGroup, &PdfViewerToolBar::scrollModeRequested);
  addModeSubmenu(tr("Spread Mode"), {tr("None"), tr("Odd"), tr("Even")}, m_spreadActions,
                 m_spreadGroup, &PdfViewerToolBar::spreadModeRequested);

  m_overflowMenu->addSeparator();

  m_documentPropertiesAction = m_overflowMenu->addAction(
      p_icons ? p_icons(QStringLiteral("info.svg")) : QIcon(), tr("Document Properties..."));
  m_documentPropertiesAction->setProperty("iconName", QStringLiteral("info.svg"));
  connect(m_documentPropertiesAction, &QAction::triggered, this,
          [this]() { emit documentPropertiesRequested(); });
}

// Placed separately from install(), and LAST of the three placement steps.
//
// The overflow button is the toolbar's catch-all, so it belongs at the very end
// -- after Readable Width, Presentation Mode and Find And Replace, all of which
// the base class appends once addAdditionalRightToolBarActions() has returned.
// Only PdfViewWindow2::setupToolBar(), which calls addRightCommonToolBarActions()
// itself, can reach that position.
QAction *PdfViewerToolBar::installOverflowAction(QToolBar *p_toolBar, const IconProvider &p_icons) {
  Q_ASSERT(p_toolBar);
  Q_ASSERT(m_overflowMenu);
  Q_ASSERT(!m_overflowAction);

  // No leading separator: the button is already the last thing on the toolbar,
  // so it reads as its own group without one, and the extra rule only adds
  // clutter next to the window edge.

  // A PLAIN action carrying its menu -- NOT addWidget() with a pre-built
  // QToolButton.
  //
  // This is the difference between the overflow menu surviving a narrow window
  // and vanishing from it. When a QToolBar runs out of room it hides the
  // trailing items and re-offers them through its own extension ("»") popup,
  // which is built by adding the hidden ACTIONS to a QMenu. A QWidgetAction
  // cannot render there, so an addWidget()-ed button simply disappears and
  // every verb behind it -- rotate, cursor, scroll mode, spread mode and
  // document properties -- becomes unreachable. The plain actions beside it
  // (Readable Width, Find and Replace) kept working, which is what made the
  // hole look like a missing button rather than a broken layout.
  //
  // With the menu on the ACTION, both surfaces work from one declaration:
  // QToolButton::menu() falls back to defaultAction()->menu() on the toolbar,
  // and QMenu renders an action-with-a-menu as a SUBMENU inside the extension
  // popup. It also makes the icon reachable by
  // ViewWindowToolBarHelper2::refreshToolBarIcons(), which only iterates the
  // toolbar's own actions.
  m_overflowAction = addIconAction(p_toolBar, QStringLiteral("menu.svg"), tr("More"), p_icons);
  m_overflowAction->setMenu(m_overflowMenu);

  // The toolbar creates the button; take it back to set the popup behaviour.
  m_overflowButton = qobject_cast<QToolButton *>(p_toolBar->widgetForAction(m_overflowAction));
  if (m_overflowButton) {
    m_overflowButton->setPopupMode(QToolButton::InstantPopup);
    // An InstantPopup ICON button, like Outline / Tag / Attachment: the whole
    // button opens the menu, so the built-in dropdown arrow is redundant
    // chrome. (This is NOT the MenuButtonPopup case in PdfAnnotationToolBar,
    // where the indicator is the entire affordance and the property must stay
    // unset.)
    m_overflowButton->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);
  }

  // install()'s enable sweep has already run, so match whatever state it left.
  m_overflowAction->setEnabled(m_state.m_valid);
  if (m_overflowButton) {
    m_overflowButton->setEnabled(m_state.m_valid);
  }
  return m_overflowAction;
}

// Deliberately NOT part of install(): this action belongs to a different region
// of the toolbar, one the base class owns. ViewWindow2 builds Readable Width and
// Find And Replace after addAdditionalRightToolBarActions() has returned, so the
// slot between them is only reachable from its own hook
// (ViewWindow2::addAdditionalViewToolBarActions).
//
// It sits there rather than in the overflow menu because it changes how the
// content is PRESENTED, which is what Readable Width beside it does -- and
// because a mode with no visible way back (the toolbar is gone once it is on)
// should at least have a visible way in.
QAction *PdfViewerToolBar::installPresentationAction(QToolBar *p_toolBar,
                                                     const IconProvider &p_icons) {
  Q_ASSERT(p_toolBar);
  Q_ASSERT(!m_presentationAction);

  m_presentationAction = addIconAction(p_toolBar, QStringLiteral("presentation_editor.svg"),
                                       tr("Presentation Mode"), p_icons);
  connect(m_presentationAction, &QAction::triggered, this,
          [this]() { emit presentationModeRequested(); });
  // install() has already run its own sweep, so match whatever state it left.
  m_presentationAction->setEnabled(m_state.m_valid);
  return m_presentationAction;
}

void PdfViewerToolBar::refreshIcons(const IconProvider &p_icons) {
  if (!p_icons) {
    return;
  }

  const auto refresh = [&p_icons](QAction *p_act) {
    if (!p_act) {
      return;
    }
    const auto name = p_act->property("iconName").toString();
    if (!name.isEmpty()) {
      p_act->setIcon(p_icons(name));
    }
  };

  // The toolbar's own actions are also covered by
  // ViewWindowToolBarHelper2::refreshToolBarIcons(); doing them here as well is
  // idempotent and keeps this component correct in isolation (the gate builds
  // it on a bare QToolBar with no helper anywhere).
  refresh(m_sidebarAction);
  refresh(m_previousPageAction);
  refresh(m_nextPageAction);
  refresh(m_zoomOutAction);
  refresh(m_zoomInAction);

  // These are the ones nothing else can reach: entries inside the overflow
  // menu. The overflow button itself is now a plain toolbar action, so
  // refreshToolBarIcons() covers it -- but it is refreshed here too, so this
  // component stays correct in isolation.
  refresh(m_overflowAction);
  refresh(m_rotateCwAction);
  refresh(m_rotateCcwAction);
  refresh(m_presentationAction);
  refresh(m_documentPropertiesAction);
}

void PdfViewerToolBar::setControlsEnabled(bool p_enabled) {
  const QList<QAction *> actions = {
      m_sidebarAction,  m_previousPageAction, m_nextPageAction,       m_zoomOutAction,
      m_zoomInAction,   m_pageSpinBoxAction,  m_pageCountLabelAction, m_zoomComboBoxAction,
      m_overflowAction, m_presentationAction};
  for (auto *act : actions) {
    if (act) {
      act->setEnabled(p_enabled);
    }
  }

  // The embedded widgets AND their widget actions: an enabled child widget
  // inside a disabled action stays interactive in some styles.
  if (m_pageSpinBox) {
    m_pageSpinBox->setEnabled(p_enabled);
  }
  if (m_zoomComboBox) {
    m_zoomComboBox->setEnabled(p_enabled);
  }
  if (m_overflowButton) {
    m_overflowButton->setEnabled(p_enabled);
  }
  if (m_overflowMenu) {
    m_overflowMenu->setEnabled(p_enabled);
  }

  // Each menu entry individually too: QAction::trigger() consults only the
  // action's OWN enabled state, so disabling the menu alone would leave every
  // row invokable.
  QList<QAction *> menuActions = {m_rotateCwAction, m_rotateCcwAction, m_documentPropertiesAction};
  menuActions += m_cursorActions;
  menuActions += m_scrollActions;
  menuActions += m_spreadActions;
  for (auto *act : menuActions) {
    if (act) {
      act->setEnabled(p_enabled);
    }
  }
}

void PdfViewerToolBar::syncModeGroup(const QList<QAction *> &p_actions, int p_value) {
  for (auto *act : p_actions) {
    // Deliberately NOT wrapped in a QSignalBlocker. setChecked() never emits
    // `triggered` (only activate() does), so there is nothing to echo back --
    // but it DOES emit `changed`, which is exactly how QActionGroup tracks its
    // current member. Blocking it leaves the group's bookkeeping stale, after
    // which a later user pick fails to clear the previous tick. The spin box
    // and the combo below follow the OPPOSITE rule; do not harmonise them.
    act->setChecked(act->data().toInt() == p_value);
  }
}

void PdfViewerToolBar::syncZoomComboBox(const ViewerState &p_state) {
  // MUST be blocked, unlike the QAction::setChecked calls above:
  // setCurrentIndex emits currentIndexChanged, and even though the user pick is
  // wired to `activated`, a future rewiring would echo straight back out.
  const QSignalBlocker blocker(m_zoomComboBox);

  int index = m_zoomComboBox->findData(p_state.m_scaleValue);
  if (index < 0) {
    // A zoom that is not one of the presets (Ctrl+wheel, Page Fit resolving to
    // an odd factor). Show it as a percentage rather than leaving a stale
    // "Automatic" ticked.
    const auto text = QStringLiteral("%1%").arg(qRound(p_state.m_scale * 100.0));
    if (m_customZoomIndex < 0) {
      m_zoomComboBox->addItem(text, p_state.m_scaleValue);
      m_customZoomIndex = m_zoomComboBox->count() - 1;
    } else {
      m_zoomComboBox->setItemText(m_customZoomIndex, text);
      m_zoomComboBox->setItemData(m_customZoomIndex, p_state.m_scaleValue);
    }
    index = m_customZoomIndex;
  }
  m_zoomComboBox->setCurrentIndex(index);
}

void PdfViewerToolBar::syncState(const ViewerState &p_state) {
  m_state = p_state;

  setControlsEnabled(p_state.m_valid);

  if (m_pageSpinBox) {
    // MUST be blocked: QSpinBox::setRange/setValue emit valueChanged, and
    // although only editingFinished is wired up, a programmatic range change
    // can also emit editingFinished on some styles.
    const QSignalBlocker blocker(m_pageSpinBox);
    m_pageSpinBox->setRange(1, qMax(1, p_state.m_pageCount));
    m_pageSpinBox->setValue(qBound(1, p_state.m_page, qMax(1, p_state.m_pageCount)));
  }

  if (m_pageCountLabel) {
    m_pageCountLabel->setText(p_state.m_valid ? tr("of %1").arg(p_state.m_pageCount) : QString());
  }

  if (m_previousPageAction) {
    m_previousPageAction->setEnabled(p_state.m_valid && p_state.m_page > 1);
  }
  if (m_nextPageAction) {
    m_nextPageAction->setEnabled(p_state.m_valid && p_state.m_pageCount > 0 &&
                                 p_state.m_page < p_state.m_pageCount);
  }

  if (m_zoomComboBox) {
    syncZoomComboBox(p_state);
  }

  if (m_sidebarAction) {
    m_sidebarAction->setChecked(p_state.m_sidebarOpen);
  }

  syncModeGroup(m_cursorActions, p_state.m_cursorTool);
  syncModeGroup(m_scrollActions, p_state.m_scrollMode);
  syncModeGroup(m_spreadActions, p_state.m_spreadMode);
}
