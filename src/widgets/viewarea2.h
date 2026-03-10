#ifndef VIEWAREA2_H
#define VIEWAREA2_H

#include <QJsonObject>
#include <QMap>
#include <QWidget>
#include <QVector>

#include <core/fileopensettings.h>
#include <core/global.h>

class QSplitter;
class QVBoxLayout;

namespace vnotex {

class ServiceLocator;
class ViewSplit2;
class ViewWindow2;
class ViewAreaController;
class Buffer2;
class ViewWindowFactory;

// ViewArea2: composite widget owning the GUI (splitter tree + ViewSplit2s) and
// ViewAreaController.  The controller knows nothing about ViewArea2.
//
// ID maps (sole source of truth):
//   m_splits  : workspaceId  (QString) -> ViewSplit2*
//   m_windows : windowId     (ID)      -> ViewWindow2*
//
// All controller signals carry IDs; ViewArea2 resolves them to pointers.
class ViewArea2 : public QWidget {
  Q_OBJECT
public:
  explicit ViewArea2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~ViewArea2() override;

  ViewAreaController *getController() const;

  // ============ Split Factory ============
  ViewSplit2 *createViewSplit(const QString &p_workspaceId);
  QSplitter *createSplitter(Qt::Orientation p_orientation);

  // ============ Layout Management ============
  ViewSplit2 *addFirstViewSplit(const QString &p_workspaceId);
  ViewSplit2 *splitAt(ViewSplit2 *p_split, Direction p_direction,
                      const QString &p_workspaceId);
  void removeViewSplit(ViewSplit2 *p_split);
  void maximizeViewSplit(ViewSplit2 *p_split);
  void distributeViewSplits();
  ViewSplit2 *findSplitByDirection(ViewSplit2 *p_split, Direction p_direction) const;

  // ============ State ============
  QVector<ViewSplit2 *> getAllViewSplits() const;
  ViewSplit2 *getCurrentViewSplit() const;
  int getViewSplitCount() const;

  // ============ Top Widget ============
  QWidget *getTopWidget() const;
  void setTopWidget(QWidget *p_widget);

  QJsonObject saveLayout() const;
  void loadLayout(const QJsonObject &p_layout);

private slots:
  // Controller signal handlers
  void onAddFirstViewSplitRequested(const QString &p_workspaceId);
  void onSplitRequested(const QString &p_workspaceId, Direction p_direction,
                        const QString &p_newWorkspaceId);
  void onRemoveViewSplitRequested(const QString &p_workspaceId);
  void onMaximizeViewSplitRequested(const QString &p_workspaceId);
  void onDistributeViewSplitsRequested();
  void onOpenBufferRequested(const Buffer2 &p_buffer, const QString &p_fileType,
                             const QString &p_workspaceId,
                             const FileOpenSettings &p_settings);
  void onCloseViewWindowRequested(ID p_windowId, bool p_force);
  void onSetCurrentViewSplitRequested(const QString &p_workspaceId, bool p_focus);
  void onFocusViewSplitRequested(const QString &p_workspaceId);
  void onMoveViewWindowToSplitRequested(ID p_windowId,
                                        const QString &p_srcWorkspaceId,
                                        const QString &p_dstWorkspaceId);
  void onLoadLayoutRequested(const QJsonObject &p_layout);

  // ViewSplit2 signal handlers
  void onMoveViewWindowOneSplitRequested(ViewSplit2 *p_split, ViewWindow2 *p_win,
                                         Direction p_direction);
  void onRemoveSplitRequested(ViewSplit2 *p_split);

private:
  void setupController();
  void wireSplitSignals(ViewSplit2 *p_split);

  ViewSplit2 *splitForWorkspace(const QString &p_workspaceId) const;
  ViewWindow2 *windowForId(ID p_windowId) const;
  ID idForWindow(ViewWindow2 *p_win) const;

  void collectViewSplits(QWidget *p_widget, QVector<ViewSplit2 *> &p_splits) const;
  static void distributeSplitter(QSplitter *p_splitter);
  QSplitter *findParentSplitter(QWidget *p_widget) const;
  void unwrapSingleChildSplitter(QSplitter *p_splitter);
  static ViewSplit2 *findFirstViewSplit(QWidget *p_widget);
  static ViewSplit2 *findLastViewSplit(QWidget *p_widget);
  static QJsonObject serializeWidget(const QWidget *p_widget);
  void deserializeSplitterNode(const QJsonObject &p_node, QSplitter *p_splitter);

  ServiceLocator &m_services;
  QVBoxLayout *m_mainLayout = nullptr;
  QWidget *m_topWidget = nullptr;
  ViewAreaController *m_controller = nullptr;

  // ID -> widget maps.  ViewArea2 is the sole owner of these mappings.
  QMap<QString, ViewSplit2 *> m_splits;  // workspaceId -> split
  QMap<ID, ViewWindow2 *> m_windows;      // windowId -> window
  ID m_nextWindowId = 1;
};

} // namespace vnotex

#endif // VIEWAREA2_H
