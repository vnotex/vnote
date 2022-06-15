#include "unitedentrymgr.h"

#include "findunitedentry.h"
#include "helpunitedentry.h"
#include "unitedentryalias.h"

#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/vnotex.h>
#include <widgets/searchinfoprovider.h>

using namespace vnotex;

UnitedEntryMgr::UnitedEntryMgr(QObject *p_parent)
    : QObject(p_parent)
{
}

void UnitedEntryMgr::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    // Built-in entries.
    const auto mainWindow = VNoteX::getInst().getMainWindow();
    addEntry(QSharedPointer<FindUnitedEntry>::create(SearchInfoProvider::create(mainWindow), this));

    addEntry(QSharedPointer<HelpUnitedEntry>::create(this));

    // Alias from config.
    const auto &config = ConfigMgr::getInst().getCoreConfig();
    const auto &aliasArr = config.getUnitedEntryAlias();
    for (int i = 0; i < aliasArr.size(); ++i) {
        auto entry = QSharedPointer<UnitedEntryAlias>::create(aliasArr[i].toObject(), this);
        addEntry(entry);
    }
}

void UnitedEntryMgr::addEntry(const QSharedPointer<IUnitedEntry> &p_entry)
{
    Q_ASSERT(!m_entries.contains(p_entry->name()));
    m_entries.insert(p_entry->name(), p_entry);
    connect(p_entry.data(), &IUnitedEntry::finished,
            this, [this]() {
                emit entryFinished(reinterpret_cast<IUnitedEntry *>(sender()));
            });
    connect(p_entry.data(), &IUnitedEntry::itemActivated,
            this, [this](bool quit, bool restoreFocus) {
                emit entryItemActivated(reinterpret_cast<IUnitedEntry *>(sender()), quit, restoreFocus);
            });
}

QList<QSharedPointer<IUnitedEntry>> UnitedEntryMgr::getEntries() const
{
    return m_entries.values();
}

QSharedPointer<IUnitedEntry> UnitedEntryMgr::findEntry(const QString &p_name) const
{
    auto it = m_entries.find(p_name);
    if (it == m_entries.end()) {
        return nullptr;
    }

    return it.value();
}

bool UnitedEntryMgr::isInitialized() const
{
    return m_initialized;
}

bool UnitedEntryMgr::getExpandAllEnabled() const
{
    return m_expandAllEnabled;
}

void UnitedEntryMgr::setExpandAllEnabled(bool p_enabled)
{
    m_expandAllEnabled = p_enabled;
}
