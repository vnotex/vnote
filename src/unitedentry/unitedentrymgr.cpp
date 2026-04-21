#include "unitedentrymgr.h"

#include "findunitedentry.h"
#include "helpunitedentry.h"
#include "historyunitedentry.h"
#include "unitedentryalias.h"

#include <core/configmgr2.h>
#include <core/coreconfig.h>

using namespace vnotex;

UnitedEntryMgr::UnitedEntryMgr(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

void UnitedEntryMgr::init() {
  if (m_initialized) {
    return;
  }

  m_initialized = true;

  // Built-in entries.
  addEntry(QSharedPointer<FindUnitedEntry>::create(m_services, this));

  addEntry(QSharedPointer<HelpUnitedEntry>::create(m_services, this));

  addEntry(QSharedPointer<HistoryUnitedEntry>::create(m_services, this));

  // Alias from config.
  const auto &config = m_services.get<ConfigMgr2>()->getCoreConfig();
  const auto &aliasArr = config.getUnitedEntryAlias();
  for (int i = 0; i < aliasArr.size(); ++i) {
    auto entry = QSharedPointer<UnitedEntryAlias>::create(aliasArr[i].toObject(), this);
    addEntry(entry);
  }
}

void UnitedEntryMgr::addEntry(const QSharedPointer<IUnitedEntry> &p_entry) {
  Q_ASSERT(!m_entries.contains(p_entry->name()));
  m_entries.insert(p_entry->name(), p_entry);
  connect(p_entry.data(), &IUnitedEntry::finished, this,
          [this]() { emit entryFinished(qobject_cast<IUnitedEntry *>(sender())); });
  connect(p_entry.data(), &IUnitedEntry::itemActivated, this, [this](bool quit, bool restoreFocus) {
    emit entryItemActivated(qobject_cast<IUnitedEntry *>(sender()), quit, restoreFocus);
  });
}

QList<QSharedPointer<IUnitedEntry>> UnitedEntryMgr::getEntries() const {
  return m_entries.values();
}

QSharedPointer<IUnitedEntry> UnitedEntryMgr::findEntry(const QString &p_name) const {
  auto it = m_entries.find(p_name);
  if (it == m_entries.end()) {
    return nullptr;
  }

  return it.value();
}

bool UnitedEntryMgr::isInitialized() const { return m_initialized; }

bool UnitedEntryMgr::getExpandAllEnabled() const { return m_expandAllEnabled; }

void UnitedEntryMgr::setExpandAllEnabled(bool p_enabled) { m_expandAllEnabled = p_enabled; }

void UnitedEntryMgr::setCurrentNotebookId(const QString &p_notebookId) {
  m_currentNotebookId = p_notebookId;
}

void UnitedEntryMgr::setCurrentFolderId(const NodeIdentifier &p_folderId) {
  m_currentFolderId = p_folderId;
}

const QString &UnitedEntryMgr::currentNotebookId() const { return m_currentNotebookId; }

const NodeIdentifier &UnitedEntryMgr::currentFolderId() const { return m_currentFolderId; }
