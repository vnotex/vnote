#ifndef WINDOWSUNITEDENTRY_H
#define WINDOWSUNITEDENTRY_H

#include "iunitedentry.h"

#include <QSharedPointer>

#include <functional>

class QTreeWidget;
class QTreeWidgetItem;

namespace vnotex {
class ServiceLocator;

// United entry (keyword "windows") that shows a two-level tree of open view
// windows grouped by workspace. Activating a window focuses it, surfacing a
// hidden workspace first if needed. Enumeration/focus go through the injected
// IViewWindowNavigator obtained from the UnitedEntryMgr.
class WindowsUnitedEntry : public IUnitedEntry {
  Q_OBJECT
public:
  WindowsUnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                     QObject *p_parent = nullptr);

  QSharedPointer<QWidget> currentPopupWidget() const Q_DECL_OVERRIDE;

protected:
  void initOnFirstProcess() Q_DECL_OVERRIDE;

  void
  processInternal(const QString &p_args,
                  const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc)
      Q_DECL_OVERRIDE;

private:
  void finish();

  void handleItemActivated(QTreeWidgetItem *p_item, int p_column);

  QSharedPointer<QTreeWidget> m_tree;
};
} // namespace vnotex

#endif // WINDOWSUNITEDENTRY_H
