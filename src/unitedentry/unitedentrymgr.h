#ifndef UNITEDENTRYMGR_H
#define UNITEDENTRYMGR_H

#include <QObject>
#include <QSharedPointer>
#include <QMap>

#include <core/noncopyable.h>

namespace vnotex
{
    class IUnitedEntry;

    class UnitedEntryMgr : public QObject, private Noncopyable
    {
        Q_OBJECT
    public:
        static UnitedEntryMgr &getInst()
        {
            static UnitedEntryMgr inst;
            inst.init();
            return inst;
        }

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
        explicit UnitedEntryMgr(QObject *p_parent = nullptr);

        void addEntry(const QSharedPointer<IUnitedEntry> &p_entry);

        bool m_initialized = false;

        QMap<QString, QSharedPointer<IUnitedEntry>> m_entries;

        bool m_expandAllEnabled = false;
    };
}

#endif // UNITEDENTRYMGR_H
