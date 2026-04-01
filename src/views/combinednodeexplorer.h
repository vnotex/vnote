#ifndef COMBINEDNODEEXPLORER_H
#define COMBINEDNODEEXPLORER_H

#include "inodeexplorer.h"

#include <QScopedPointer>

namespace vnotex {

class NotebookNodeModel;
class NotebookNodeProxyModel;
class NotebookNodeView;
class NotebookNodeDelegate;
class NotebookNodeController;
class NavigationMode;
template <typename T> class NavigationModeViewWrapper;
class ServiceLocator;

// CombinedNodeExplorer encapsulates a single-panel view showing both folders and files.
// Implements INodeExplorer interface for polymorphic usage in NotebookExplorer2.
class CombinedNodeExplorer : public INodeExplorer {
  Q_OBJECT

public:
  explicit CombinedNodeExplorer(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~CombinedNodeExplorer() override;

  // === INodeExplorer interface implementation ===

  void setNotebookId(const QString &p_notebookId) override;
  QString getNotebookId() const override;

  NodeIdentifier currentNodeId() const override;
  QList<NodeIdentifier> selectedNodeIds() const override;

  void selectNode(const NodeIdentifier &p_nodeId) override;
  void expandToNode(const NodeIdentifier &p_nodeId) override;
  void scrollToNode(const NodeIdentifier &p_nodeId) override;

  void expandAll() override;
  void collapseAll() override;

  NodeIdentifier currentFolderId() const override;

  void setViewOrder(ViewOrder p_order) override;

  NodeInfo getNodeInfo(const NodeIdentifier &p_nodeId) const override;

  void handleRenameResult(const NodeIdentifier &p_nodeId, const QString &p_newName) override;
  void handleDeleteConfirmed(const QList<NodeIdentifier> &p_nodeIds, bool p_permanent) override;
  void handleRemoveConfirmed(const QList<NodeIdentifier> &p_nodeIds) override;

  void reloadNode(const NodeIdentifier &p_nodeId) override;
  void startInlineRename(const NodeIdentifier &p_nodeId) override;

  // === External files visibility ===
  void setExternalNodesVisible(bool p_visible) override;

  NavigationMode *getNavigationModeWrapper() const;

private slots:
  void onContextMenuRequested(const NodeIdentifier &p_nodeId, const QPoint &p_globalPos);

private:
  void setupUI();

  ServiceLocator &m_services;
  QString m_notebookId;

  // Single panel components
  NotebookNodeModel *m_model = nullptr;
  NotebookNodeProxyModel *m_proxyModel = nullptr;
  NotebookNodeView *m_view = nullptr;
  QScopedPointer<NavigationModeViewWrapper<NotebookNodeView>> m_navigationWrapper;
  NotebookNodeDelegate *m_delegate = nullptr;
  NotebookNodeController *m_controller = nullptr;
};

} // namespace vnotex

#endif // COMBINEDNODEEXPLORER_H
