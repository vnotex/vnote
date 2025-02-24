#ifndef NOTEBOOKSELECTOR2_H
#define NOTEBOOKSELECTOR2_H

#include <QJsonArray>
#include <QModelIndex>

#include "combobox.h"
#include "core/global.h"
#include "navigationmode.h"
#include "notebooknodeexplorer.h"

namespace vnotex {

class ServiceLocator;
class Notebook;

// NotebookSelector2 - Migrated version using ServiceLocator DI pattern.
// Replaces deprecated NotebookSelector which uses VNoteX::getInst().getNotebookMgr().
class NotebookSelector2 : public ComboBox, public NavigationMode {
  Q_OBJECT

public:
  explicit NotebookSelector2(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  void loadNotebooks();

  void reloadNotebook(const Notebook *p_notebook);

  void setCurrentNotebook(ID p_id);

  void setViewOrder(int p_order);

signals:
  void newNotebookRequested();

  // NavigationMode.
protected:
  QVector<void *> getVisibleNavigationItems() override;

  void placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) override;

  void handleTargetHit(void *p_item) override;

  void clearNavigation() override;

protected:
  bool eventFilter(QObject *p_obj, QEvent *p_event) override;

  void mousePressEvent(QMouseEvent *p_event) override;

private:
  void addNotebookItem(const QJsonObject &p_notebookJson);

  QIcon generateItemIcon(const QString &p_name, const QIcon &p_customIcon);

  QString generateItemToolTip(const QString &p_name, const QString &p_rootPath,
                              const QString &p_description);

  QString getItemToolTip(int p_idx) const;
  void setItemToolTip(int p_idx, const QString &p_tooltip);

  int findNotebook(ID p_id) const;

  void sortNotebooks(QJsonArray &p_notebooks) const;

  static void fetchIconColor(const QString &p_name, QString &p_fg, QString &p_bg);

  ServiceLocator &m_services;

  bool m_notebooksInitialized = false;

  QVector<QModelIndex> m_navigationIndexes;

  ViewOrder m_viewOrder = ViewOrder::OrderedByConfiguration;
};

} // namespace vnotex

#endif // NOTEBOOKSELECTOR2_H
