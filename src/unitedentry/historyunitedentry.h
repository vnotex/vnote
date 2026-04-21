#ifndef HISTORYUNITEDENTRY_H
#define HISTORYUNITEDENTRY_H

#include "iunitedentry.h"

#include <QSharedPointer>
#include <functional>

#include <unitedentry/unitedentryhelper.h>

class QListView;

namespace vnotex {

class ServiceLocator;
class HistoryListModel;
class FileNodeDelegate;

class HistoryUnitedEntry : public IUnitedEntry {
  Q_OBJECT

public:
  HistoryUnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                     QObject *p_parent = nullptr);

  QSharedPointer<QWidget> currentPopupWidget() const Q_DECL_OVERRIDE;

protected:
  void initOnFirstProcess() Q_DECL_OVERRIDE;

  void processInternal(
      const QString &p_args,
      const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc) Q_DECL_OVERRIDE;

private:
  void finish();

  void handleItemActivated(const QModelIndex &p_index);

  ServiceLocator &m_services;

  HistoryListModel *m_model = nullptr;

  QSharedPointer<QWidget> m_listView;

  FileNodeDelegate *m_delegate = nullptr;

  UnitedEntryHelper m_helper;
};

} // namespace vnotex

#endif // HISTORYUNITEDENTRY_H
