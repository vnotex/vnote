#ifndef UNITEDENTRYMGR_H
#define UNITEDENTRYMGR_H

#include <QMap>
#include <QObject>
#include <QSharedPointer>

#include <core/nodeidentifier.h>
#include <core/noncopyable.h>
#include <core/servicelocator.h>

namespace vnotex {
class IUnitedEntry;
class IViewWindowNavigator;

class UnitedEntryMgr : public QObject, private Noncopyable {
  Q_OBJECT
public:
  explicit UnitedEntryMgr(ServiceLocator &p_services, QObject *p_parent = nullptr);

  void init();

  QList<QSharedPointer<IUnitedEntry>> getEntries() const;

  QSharedPointer<IUnitedEntry> findEntry(const QString &p_name) const;

  // Exact match wins; otherwise resolves a unique prefix; null if none or ambiguous.
  QSharedPointer<IUnitedEntry> resolveEntry(const QString &p_name) const;

  // Pure name resolver (exposed for unit tests): returns p_name if it is an exact
  // element of p_names; else the single element that startsWith(p_name); else empty
  // QString (no match OR ambiguous OR empty input).
  static QString resolveEntryName(const QList<QString> &p_names, const QString &p_name);

  bool isInitialized() const;

  bool getExpandAllEnabled() const;
  void setExpandAllEnabled(bool p_enabled);

  const QString &currentNotebookId() const;
  const NodeIdentifier &currentFolderId() const;

  // View window navigator seam (set by MainWindow2). Used by WindowsUnitedEntry
  // to enumerate and focus open view windows. May be null in tests / early init.
  void setViewWindowNavigator(IViewWindowNavigator *p_navigator);
  IViewWindowNavigator *viewWindowNavigator() const;

public slots:
  void setCurrentNotebookId(const QString &p_notebookId);
  void setCurrentFolderId(const NodeIdentifier &p_folderId);

signals:
  void entryFinished(IUnitedEntry *p_entry);

  void entryItemActivated(IUnitedEntry *p_entry, bool p_quit, bool p_restoreFocus);

private:
  void addEntry(const QSharedPointer<IUnitedEntry> &p_entry);

  ServiceLocator &m_services;

  bool m_initialized = false;

  QMap<QString, QSharedPointer<IUnitedEntry>> m_entries;

  bool m_expandAllEnabled = false;

  IViewWindowNavigator *m_windowNavigator = nullptr;

  QString m_currentNotebookId;

  NodeIdentifier m_currentFolderId;
};
} // namespace vnotex

#endif // UNITEDENTRYMGR_H
