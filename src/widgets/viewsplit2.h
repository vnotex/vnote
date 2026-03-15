#ifndef VIEWSPLIT2_H
#define VIEWSPLIT2_H

#include <QIcon>
#include <QTabWidget>
#include <QString>
#include <QVector>

#include <core/global.h>

class QActionGroup;
class QToolButton;
class QMenu;

namespace vnotex {

class ServiceLocator;
class ViewWindow2;

// QTabWidget-based view split for managing ViewWindow2 tabs in a single vxcore workspace.
// Represents one split pane that contains multiple view windows as tabs.
//
// Responsibilities:
// - Manage a set of ViewWindow2 tabs mapped to a vxcore workspace
// - Route tab signals (close, focus, status) to appropriate handlers
// - Emit split-level events (focused, maximized, split requested)
// - Track active state for UI rendering
//
// Usage:
//   ViewSplit2 split(services, "workspace-id");
//   split.addViewWindow(viewWin);
//   connect(&split, &ViewSplit2::focused, [this](auto s) { activateSplit(s); });
class ViewSplit2 : public QTabWidget {
  Q_OBJECT
public:
  // Constructor.
  // @p_services: ServiceLocator for dependency injection.
  // @p_workspaceId: vxcore workspace ID this split is mapped to.
  // @p_parent: Parent widget (optional).
  explicit ViewSplit2(ServiceLocator &p_services, const QString &p_workspaceId,
                      QWidget *p_parent = nullptr);

  ~ViewSplit2() override;

  // ============ Tab Management ============

  // Add a view window as a new tab. Takes ownership of p_win (reparented).
  // Connects to p_win's signals (focused, statusChanged, nameChanged).
  void addViewWindow(ViewWindow2 *p_win);

  // Remove a view window from tabs without deleting it.
  // Disconnects signals. Sets p_win's parent to nullptr.
  void takeViewWindow(ViewWindow2 *p_win);

  // Get the currently active view window, or nullptr if no tabs.
  ViewWindow2 *getCurrentViewWindow() const;

  // Set the current tab to p_win. No-op if p_win is nullptr.
  void setCurrentViewWindow(ViewWindow2 *p_win);

  // Set the current tab by index. No-op if index is out of range.
  void setCurrentViewWindow(int p_idx);

  // Get all view windows in tab order.
  QVector<ViewWindow2 *> getAllViewWindows() const;

  // Get the number of open tabs.
  int getViewWindowCount() const;

  // ============ Workspace ============

  // Get the vxcore workspace ID this split is mapped to.
  const QString &getWorkspaceId() const;

  // Set the vxcore workspace ID this split is mapped to.
  // Used when switching workspaces on an existing split.
  void setWorkspaceId(const QString &p_workspaceId);

  // ============ State ============

  // Set visual active state (highlighted corner icons etc.).
  void setActive(bool p_active);

  // Whether this split is visually active.
  bool isActive() const;

  // Focus the current view window, or focus self if no tabs.
  void focus();

signals:
  // Emitted when a tab close button is clicked.
  void viewWindowCloseRequested(ViewWindow2 *p_win);

  // Emitted when this split gains focus (tab bar click, mouse press, child focus).
  void focused(ViewSplit2 *p_split);

  // Emitted when the current tab changes.
  void currentViewWindowChanged(ViewWindow2 *p_win);

  // Request to split this view split.
  // @p_direction: Left/Right for vertical split, Up/Down for horizontal split.
  void splitRequested(ViewSplit2 *p_split, Direction p_direction);

  // Request to remove this split.
  void removeSplitRequested(ViewSplit2 *p_split);

  // Request to remove this split AND destroy its workspace.
  void removeSplitAndWorkspaceRequested(ViewSplit2 *p_split);

  // Request to maximize this split.
  void maximizeSplitRequested(ViewSplit2 *p_split);

  // Request to distribute all splits evenly.
  void distributeSplitsRequested();

  // Request to move a view window to an adjacent split.
  void moveViewWindowOneSplitRequested(ViewSplit2 *p_split, ViewWindow2 *p_win,
                                       Direction p_direction);

  // Request to create a new workspace for this split.
  void newWorkspaceRequested(ViewSplit2 *p_split);

  // Request to remove the workspace of this split.
  void removeWorkspaceRequested(ViewSplit2 *p_split);

  // Request to switch to a different workspace.
  void switchWorkspaceRequested(ViewSplit2 *p_split, const QString &p_workspaceId);

  // Emitted when tabs are reordered via drag. Carries the new buffer ID order.
  void bufferOrderChanged(ViewSplit2 *p_split, const QStringList &p_bufferIds);

protected:
  void mousePressEvent(QMouseEvent *p_event) override;

  bool eventFilter(QObject *p_object, QEvent *p_event) override;

private slots:
  void closeTab(int p_idx);

private:
  void setupUI();
  void setupTabBar();
  void setupCornerWidget();

  void initIcons();

  void updateWindowList(QMenu *p_menu);
  void updateMenu(QMenu *p_menu);

  ViewWindow2 *getViewWindow(int p_idx) const;

  void focusCurrentViewWindow();

  ServiceLocator &m_services;

  QString m_workspaceId;

  bool m_active = false;

  // Used for alternate tab tracking.
  ViewWindow2 *m_currentViewWindow = nullptr;
  ViewWindow2 *m_lastViewWindow = nullptr;

  // Corner widget buttons.
  QToolButton *m_windowListButton = nullptr;
  QToolButton *m_menuButton = nullptr;

  // Action groups for menu items (lazy-initialized).
  QActionGroup *m_windowListActionGroup = nullptr;

  // Icons (static, shared across all ViewSplit2 instances).
  static QIcon s_windowListIcon;
  static QIcon s_windowListActiveIcon;
  static QIcon s_menuIcon;
  static QIcon s_menuActiveIcon;

  static const QString c_actionButtonForegroundName;
  static const QString c_activeActionButtonForegroundName;
};

} // namespace vnotex

#endif // VIEWSPLIT2_H
