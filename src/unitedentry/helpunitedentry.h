#ifndef HELPUNITEDENTRY_H
#define HELPUNITEDENTRY_H

#include "iunitedentry.h"

#include <QSharedPointer>

class QTreeWidget;

namespace vnotex
{
    class HelpUnitedEntry : public IUnitedEntry
    {
        Q_OBJECT
    public:
        HelpUnitedEntry(UnitedEntryMgr *p_mgr, QObject *p_parent = nullptr);

        QSharedPointer<QWidget> currentPopupWidget() const Q_DECL_OVERRIDE;

    protected:
        void initOnFirstProcess() Q_DECL_OVERRIDE;

        void processInternal(const QString &p_args,
                             const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc) Q_DECL_OVERRIDE;

    private:
        QSharedPointer<QTreeWidget> m_infoTree;
    };
}

#endif // HELPUNITEDENTRY_H
