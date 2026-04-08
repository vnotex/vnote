#ifndef UNITEDENTRYMGR_H
#define UNITEDENTRYMGR_H

#include <QMap>
#include <QObject>
#include <QSharedPointer>

#include <core/noncopyable.h>
#include <core/servicelocator.h>

namespace vnotex {
class IUnitedEntry;

class UnitedEntryMgr : public QObject, private Noncopyable {
  Q_OBJECT
public:
  explicit UnitedEntryMgr(ServiceLocator &p_services, QObject *p_parent = nullptr);

  void init();

  QList<QSharedPointer<IUnitedEntry>> getEntries() const;

  QSharedPointer<IUnitedEntry> findEntry(const QString &p_name) const;

  bool isInitialized() const;

  bool getExpandAllEnabled() const;
  void setExpandAllEnabled(bool p_enabled);

signals:
  void entryFinished(IUnitedEntry *p_entry);

  void entryItemActivated(IUnitedEntry *p_entry, bool p_quit, bool p_restoreFocus);

private:
  void addEntry(const QSharedPointer<IUnitedEntry> &p_entry);

  ServiceLocator &m_services;

  bool m_initialized = false;

  QMap<QString, QSharedPointer<IUnitedEntry>> m_entries;

  bool m_expandAllEnabled = false;
};
} // namespace vnotex

#endif // UNITEDENTRYMGR_H
