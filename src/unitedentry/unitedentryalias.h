#ifndef UNITEDENTRYALIAS_H
#define UNITEDENTRYALIAS_H

#include "iunitedentry.h"

#include <QJsonObject>

#include "unitedentryhelper.h"

namespace vnotex
{
    class UnitedEntryMgr;

    // UnitedEntry which points to another UnitedEntry.
    class UnitedEntryAlias : public IUnitedEntry
    {
        Q_OBJECT
    public:
        UnitedEntryAlias(const QString &p_name,
                         const QString &p_description,
                         const QString &p_value,
                         UnitedEntryMgr *p_mgr,
                         QObject *p_parent = nullptr);

        UnitedEntryAlias(const QJsonObject &p_obj,
                         UnitedEntryMgr *p_mgr,
                         QObject *p_parent = nullptr);

        QJsonObject toJson() const;

        QString description() const Q_DECL_OVERRIDE;

        bool isOngoing() const Q_DECL_OVERRIDE;

        void handleAction(Action p_act) Q_DECL_OVERRIDE;

        QSharedPointer<QWidget> currentPopupWidget() const Q_DECL_OVERRIDE;

        bool isAliasOf(const IUnitedEntry *p_entry) const Q_DECL_OVERRIDE;

    protected:
        void initOnFirstProcess() Q_DECL_OVERRIDE;

        void processInternal(const QString &p_args,
                             const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc) Q_DECL_OVERRIDE;

        void setOngoing(bool p_ongoing) Q_DECL_OVERRIDE;

    private:
        QString m_value;

        UnitedEntryHelper::UserEntry m_alias;

        IUnitedEntry *m_realEntry = nullptr;
    };
}

#endif // UNITEDENTRYALIAS_H
