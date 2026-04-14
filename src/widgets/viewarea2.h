#ifndef VIEWAREA2_H
#define VIEWAREA2_H

#include <QJsonObject>
#include <QMap>
#include <QVector>
#include <QWidget>

#include <controllers/viewareaview.h>
#include <core/fileopensettings.h>
#include <core/global.h>

#include "navigationmode.h"
#include "viewsplit2.h"

class QLabel;
class QSplitter;
class QStackedLayout;
class QVBoxLayout;

namespace vnotex {

class ServiceLocator;
class ViewSplit2;
class ViewWindow2;
class ViewAreaController;
class ViewAreaHomeWidget;
class Buffer2;
class ViewWindowFactory;
class IViewWindowContent;

// ViewArea2: composite widget owning the GUI (splitter tree + ViewSplit2s) and
// ViewAreaController.  The controller knows nothing about ViewArea2.
// Implements ViewAreaView so the controller can call view commands directly.
//
// ID maps (sole source of truth):
//   m_splits  : workspaceId  (QString) -> ViewSplit2*
//   m_windows : windowId     (ID)      -> ViewWindow2*
class ViewArea2 : public QWidget, public ViewAreaView, public NavigationMode {
  Q_OBJECT
public:
  explicit ViewArea2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~ViewArea2() override;

  ViewAreaController *getController() const;

  // ============ Split Factory ============
  ViewSplit2 *createViewSplit(const QString &p_workspaceId);
  QSplitter *createSplitter(Qt::Orientation p_orientation);

  // ============ Layout Management (widget-level) ============
  ViewSplit2 *addFirstViewSplitWidget(const QString &p_workspaceId);
  ViewSplit2 *splitAt(ViewSplit2 *p_split, Direction p_direction, const QString &p_workspaceId);
  void removeViewSplitWidget(ViewSplit2 *p_split);
  void maximizeViewSplitWidget(ViewSplit2 *p_split);
  void distributeViewSplitWidgets();
  ViewSplit2 *findSplitByDirection(ViewSplit2 *p_split, Direction p_direction) const;

  // ============ State ============
  QVector<ViewSplit2 *> getAllViewSplits() const;
  ViewSplit2 *getCurrentViewSplit() const;
  int getViewSplitCount() const override;

  // ============ Top Widget ============
  QWidget *getTopWidget() const;
  void setTopWidget(QWidget *p_widget);

  QJsonObject saveLayout() const;
  void loadLayoutFromSession(const QJsonObject &p_layout);

  // Get the currently active ViewWindow2 (nullptr if none).
  ViewWindow2 *getCurrentViewWindow() const;

  // Save workspace/buffer session state to vxcore and call vxcore_shutdown().
  // Called before closing buffers on exit.
  void saveSession();

  // Restore workspace/buffer session state from vxcore.
  // Called after loadLayout() has created the splitter tree.
  void restoreSession();

  // ============ ViewAreaView Interface (overrides) ============
  void addFirstViewSplit(const QString &p_workspaceId) override;
  void split(const QString &p_workspaceId, Direction p_direction,
             const QString &p_newWorkspaceId) override;
  void removeViewSplit(const QString &p_workspaceId) override;
  void maximizeViewSplit(const QString &p_workspaceId) override;
  void distributeViewSplits() override;
  void openBuffer(const Buffer2 &p_buffer, const QString &p_fileType, const QString &p_workspaceId,
                  const FileOpenSettings &p_settings) override;
  void openWidgetContent(IViewWindowContent *p_content, const Buffer2 &p_buffer,
                         const QString &p_workspaceId) override;
  bool closeViewWindow(ID p_windowId, bool p_force) override;
  void applyFileOpenSettings(ID p_windowId, const FileOpenSettings &p_settings) override;
  void setCurrentViewSplit(const QString &p_workspaceId, bool p_focus) override;
  void focusViewSplit(const QString &p_workspaceId) override;
  void moveViewWindowToSplit(ID p_windowId, const QString &p_srcWorkspaceId,
                             const QString &p_dstWorkspaceId) override;
  void switchWorkspace(const QString &p_currentWorkspaceId,
                       const QString &p_newWorkspaceId) override;
  QVector<QObject *> takeViewWindowsFromSplit(const QString &p_workspaceId,
                                              int *p_outCurrentIndex) override;
  void placeViewWindowsInSplit(const QString &p_workspaceId, const QVector<QObject *> &p_windows,
                               int p_currentIndex) override;
  void updateSplitWorkspaceId(const QString &p_oldWorkspaceId,
                              const QString &p_newWorkspaceId) override;
  void loadLayout(const QJsonObject &p_layout) override;
  void setCurrentBuffer(const QString &p_workspaceId, const QString &p_bufferId,
                        bool p_focus) override;
  QStringList getVisibleWorkspaceIds() const override;
  QVector<ID> getViewWindowIdsForWorkspace(const QString &p_workspaceId) const override;
  ID findWindowIdByBufferId(const QString &p_workspaceId, const QString &p_bufferId) const override;
  QString getCurrentBufferIdForWorkspace(const QString &p_workspaceId) const override;
  void onNodeRenamed(const NodeIdentifier &p_oldNodeId, const NodeIdentifier &p_newNodeId) override;
  void notifyEditorConfigChanged() override;

protected:
  // NavigationMode overrides.
  QVector<void *> getVisibleNavigationItems() override;
  void placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) override;
  void handleTargetHit(void *p_item) override;
  void clearNavigation() override;

private slots:
  // ViewSplit2 signal handlers
  void onMoveViewWindowOneSplitRequested(ViewSplit2 *p_split, ViewWindow2 *p_win,
                                         Direction p_direction);
  void onRemoveSplitRequested(ViewSplit2 *p_split);
  void onRemoveSplitAndWorkspaceRequested(ViewSplit2 *p_split);

private:
  void setupController();
  void setupShortcuts();
  void wireSplitSignals(ViewSplit2 *p_split);

  ViewSplit2 *splitForWorkspace(const QString &p_workspaceId) const;
  ViewWindow2 *windowForId(ID p_windowId) const;

  void collectViewSplits(QWidget *p_widget, QVector<ViewSplit2 *> &p_splits) const;
  static void distributeSplitter(QSplitter *p_splitter);
  QSplitter *findParentSplitter(QWidget *p_widget) const;
  void unwrapSingleChildSplitter(QSplitter *p_splitter);
  static ViewSplit2 *findFirstViewSplit(QWidget *p_widget);
  static ViewSplit2 *findLastViewSplit(QWidget *p_widget);
  static QJsonObject serializeWidget(const QWidget *p_widget);
  void deserializeSplitterNode(const QJsonObject &p_node, QSplitter *p_splitter);

  // Screen switching: show Home when no windows, Contents when >= 1.
  void showHomeScreen();
  void showContentsScreen();
  void updateScreenVisibility();

  // Stack page indices.
  enum StackPage { HomeScreen = 0, ContentsScreen = 1 };

  ServiceLocator &m_services;
  QStackedLayout *m_stackedLayout = nullptr;

  // Page 0: placeholder home widget.
  ViewAreaHomeWidget *m_homeWidget = nullptr;

  // Page 1: container for the splitter tree / single ViewSplit2.
  QWidget *m_contentsWidget = nullptr;
  QVBoxLayout *m_contentsLayout = nullptr;

  QWidget *m_topWidget = nullptr;
  ViewAreaController *m_controller = nullptr;

  // ID -> widget maps.  ViewArea2 is the sole owner of these mappings.
  QMap<QString, ViewSplit2 *> m_splits; // workspaceId -> split
  QMap<ID, ViewWindow2 *> m_windows;    // windowId -> window
  QVector<ViewSplit2::TabNavigationInfo> m_navigationItems;
  ID m_nextWindowId = 1;
};

} // namespace vnotex

#endif // VIEWAREA2_H
