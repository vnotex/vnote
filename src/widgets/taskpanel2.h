#ifndef TASKPANEL2_H
#define TASKPANEL2_H

#include <QFrame>
#include <core/noncopyable.h>

class QTreeView;

namespace vnotex {
class ServiceLocator;
class TitleBar;
class TaskTreeModel;
class TaskController;

// Tasks dock view (left, tabified with Snippets). Lists app + notebook tasks in
// a grouped tree, and routes Run / Open file / Delete / New / Refresh through
// TaskController. Pure view: never runs tasks or touches files directly.
class TaskPanel2 : public QFrame, private Noncopyable {
  Q_OBJECT

public:
  explicit TaskPanel2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~TaskPanel2() override = default;

  void initialize();

private slots:
  void onItemActivated(const QModelIndex &p_index);
  void onContextMenuRequested(const QPoint &p_pos);

private:
  void setupUI();
  void setupTitleBar();

  ServiceLocator &m_services;

  TitleBar *m_titleBar = nullptr;

  QTreeView *m_treeView = nullptr;

  TaskTreeModel *m_model = nullptr;

  TaskController *m_controller = nullptr;
};

} // namespace vnotex
#endif // TASKPANEL2_H
