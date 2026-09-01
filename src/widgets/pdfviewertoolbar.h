#ifndef PDFVIEWERTOOLBAR_H
#define PDFVIEWERTOOLBAR_H

#include <functional>

#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>

#include "editors/pdfvieweradapter.h"

class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QMenu;
class QSpinBox;
class QToolBar;
class QToolButton;

namespace vnotex {

// Builds and owns the native replacements for pdf.js's built-in toolbar strip
// (which pdfviewer.css hides) on a caller-supplied QToolBar: sidebar toggle,
// page navigation, zoom, and an overflow menu covering the secondary toolbar.
//
// Deliberately NOT a QWidget and deliberately NOT holding a ServiceLocator, for
// exactly the reason PdfAnnotationToolBar is not: it must be constructible in a
// test with a bare QToolBar, no PdfViewWindow2 and no WebEngine profile. The
// icon provider is injected for the same link-isolation reason
// (ViewWindowToolBarHelper2::generateIcon drags ThemeService in); an empty
// provider yields icon-less, text-only actions, which is what the test uses.
//
// It emits INTENTS and never talks to the adapter. syncState() repaints
// everything from the adapter's state, which is the single source of truth: the
// page can change the page, the zoom, the rotation and every mode by itself
// (scrolling, an outline click, a keyboard shortcut, presentation mode forcing
// page-scroll), so a tick painted optimistically from the user's click would
// diverge.
class PdfViewerToolBar : public QObject {
  Q_OBJECT
public:
  using IconProvider = std::function<QIcon(const QString &p_iconName)>;

  using ViewerState = PdfViewerAdapter::ViewerState;

  explicit PdfViewerToolBar(QObject *p_parent = nullptr);

  // Creates the actions, widgets and menus on @p_toolBar. Call once.
  //
  // Placement happens in THREE steps, because the toolbar is shared with the
  // base class and two of the positions are only reachable later:
  //
  //   1. install()                   -- sidebar, Outline hook, page, zoom, and
  //                                     the overflow MENU's contents
  //   2. installPresentationAction() -- from ViewWindow2::addAdditionalViewToolBarActions()
  //   3. installOverflowAction()     -- after ViewWindow2::addRightCommonToolBarActions()
  //
  // @p_afterSidebar runs between the sidebar toggle and the page controls, and
  // is where PdfViewWindow2 inserts the Outline popup. It is a hook rather than
  // a fourth step because the popup needs a ServiceLocator and an
  // OutlineProvider, and this component deliberately holds neither.
  void install(QToolBar *p_toolBar, const IconProvider &p_icons = {},
               const std::function<void()> &p_afterSidebar = {});

  // Adds Presentation Mode to @p_toolBar and returns it.
  //
  // Separate from install() because it belongs to a region the BASE class owns:
  // ViewWindow2 adds Readable Width and Find And Replace after
  // addAdditionalRightToolBarActions() has returned, so the slot between them is
  // reachable only from ViewWindow2::addAdditionalViewToolBarActions(). It sits
  // there rather than in the overflow menu because it changes how the content is
  // presented, exactly like Readable Width beside it.
  QAction *installPresentationAction(QToolBar *p_toolBar, const IconProvider &p_icons = {});

  // Adds the overflow ("More") entry that opens the menu install() built, and
  // returns it.
  //
  // LAST of the three steps: the catch-all button belongs at the very end of
  // the toolbar, after Readable Width, Presentation Mode and Find And Replace,
  // which the base class appends once addAdditionalRightToolBarActions() has
  // returned. Only PdfViewWindow2::setupToolBar() can reach that position.
  QAction *installOverflowAction(QToolBar *p_toolBar, const IconProvider &p_icons = {});

  // Re-supply the icons after a theme switch.
  //
  // ViewWindowToolBarHelper2::refreshToolBarIcons() covers the plain actions
  // for free (they carry an `iconName` property and live directly on the
  // toolbar), but it iterates the TOOLBAR's actions only: an entry inside the
  // overflow menu is never reached, and setting an icon on the widget-action of
  // an addWidget()-ed QToolButton does not repaint the button. Hence this.
  void refreshIcons(const IconProvider &p_icons);

  // Repaints every control from the caller's state. Controls stay DISABLED
  // until the first accepted state (p_state.m_valid), so a blank window has no
  // live controls rather than controls that silently do nothing.
  void syncState(const ViewerState &p_state);

  // ---- Accessors, for the gate ----
  QAction *sidebarAction() const { return m_sidebarAction; }
  QAction *previousPageAction() const { return m_previousPageAction; }
  QAction *nextPageAction() const { return m_nextPageAction; }
  QSpinBox *pageSpinBox() const { return m_pageSpinBox; }
  QLabel *pageCountLabel() const { return m_pageCountLabel; }
  QAction *zoomOutAction() const { return m_zoomOutAction; }
  QAction *zoomInAction() const { return m_zoomInAction; }
  QComboBox *zoomComboBox() const { return m_zoomComboBox; }
  QToolButton *overflowButton() const { return m_overflowButton; }
  // The toolbar action that carries the overflow menu. It is a PLAIN action
  // with `menu()` set, not a QWidgetAction -- that is what lets QToolBar's
  // extension popup re-offer it as a submenu when the window is too narrow.
  QAction *overflowAction() const { return m_overflowAction; }
  QMenu *overflowMenu() const { return m_overflowMenu; }
  QAction *rotateClockwiseAction() const { return m_rotateCwAction; }
  QAction *rotateCounterClockwiseAction() const { return m_rotateCcwAction; }
  QAction *presentationModeAction() const { return m_presentationAction; }
  QAction *documentPropertiesAction() const { return m_documentPropertiesAction; }
  const QList<QAction *> &cursorToolActions() const { return m_cursorActions; }
  const QList<QAction *> &scrollModeActions() const { return m_scrollActions; }
  const QList<QAction *> &spreadModeActions() const { return m_spreadActions; }

signals:
  // 1-based, as pdf.js counts pages.
  void pageRequested(int p_page);

  // 'auto' | 'page-actual' | 'page-fit' | 'page-width' | a numeric string.
  void zoomRequested(const QString &p_value);

  void zoomStepRequested(bool p_zoomIn);

  // ABSOLUTE degrees (0/90/180/270), computed from the last synced state --
  // the adapter's command is absolute, so the delta is resolved here rather
  // than duplicating "current rotation" on both sides.
  void rotationRequested(int p_degrees);

  void scrollModeRequested(int p_mode);

  void spreadModeRequested(int p_mode);

  void cursorToolRequested(int p_tool);

  void sidebarToggleRequested();

  void presentationModeRequested();

  void documentPropertiesRequested();

private:
  QAction *addIconAction(QToolBar *p_toolBar, const QString &p_iconName, const QString &p_text,
                         const IconProvider &p_icons);

  // One exclusive radio group in the overflow menu. Fills @p_actions with the
  // created entries, whose data() carries the mode value, and @p_group with the
  // group that owns them.
  QMenu *addModeSubmenu(const QString &p_title, const QList<QString> &p_labels,
                        QList<QAction *> &p_actions, QActionGroup *&p_group,
                        void (PdfViewerToolBar::*p_signal)(int));

  void buildOverflowMenu(QToolBar *p_toolBar, const IconProvider &p_icons);

  void setControlsEnabled(bool p_enabled);

  // Selects the entry whose data() equals @p_value, materializing a "custom
  // percentage" row when the live zoom is not one of the presets.
  void syncZoomComboBox(const ViewerState &p_state);

  // Ticks the one entry whose data() equals @p_value and unticks the rest.
  static void syncModeGroup(const QList<QAction *> &p_actions, int p_value);

  // Everything below is owned by the toolbar / the menus / this QObject.
  QAction *m_sidebarAction = nullptr;
  QAction *m_previousPageAction = nullptr;
  QAction *m_nextPageAction = nullptr;
  QAction *m_zoomOutAction = nullptr;
  QAction *m_zoomInAction = nullptr;

  QSpinBox *m_pageSpinBox = nullptr;
  QAction *m_pageSpinBoxAction = nullptr;
  QLabel *m_pageCountLabel = nullptr;
  QAction *m_pageCountLabelAction = nullptr;
  QComboBox *m_zoomComboBox = nullptr;
  QAction *m_zoomComboBoxAction = nullptr;

  QToolButton *m_overflowButton = nullptr;
  QAction *m_overflowAction = nullptr;
  QMenu *m_overflowMenu = nullptr;

  QAction *m_rotateCwAction = nullptr;
  QAction *m_rotateCcwAction = nullptr;
  QAction *m_presentationAction = nullptr;
  QAction *m_documentPropertiesAction = nullptr;

  QActionGroup *m_cursorGroup = nullptr;
  QActionGroup *m_scrollGroup = nullptr;
  QActionGroup *m_spreadGroup = nullptr;
  QList<QAction *> m_cursorActions;
  QList<QAction *> m_scrollActions;
  QList<QAction *> m_spreadActions;

  // Separators, so a theme refresh / enable sweep can skip them and the gate
  // can assert the layout.
  QList<QAction *> m_separators;

  // The last synced state, used to resolve a rotate DELTA into the absolute
  // degrees the adapter's command takes.
  ViewerState m_state;

  // Lazily appended row showing a zoom percentage that is not one of the
  // presets (e.g. a Ctrl+wheel zoom landing on 137%). Kept rather than
  // recreated, so the combo's item count is stable.
  int m_customZoomIndex = -1;
};

} // namespace vnotex

#endif // PDFVIEWERTOOLBAR_H
