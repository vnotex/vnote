#ifndef EDITREADDISCARDACTION_H
#define EDITREADDISCARDACTION_H

#include "biaction.h"

#include <QScopedPointer>

namespace vnotex
{
    class EditReadDiscardAction : public BiAction
    {
        Q_OBJECT
    public:
        enum Action
        {
            Edit,
            Read,
            Discard
        };

        EditReadDiscardAction(const QIcon &p_editIcon,
                              const QString &p_editText,
                              const QIcon &p_readIcon,
                              const QString &p_readText,
                              const QIcon &p_discardIcon,
                              const QString &p_discardText,
                              QObject *p_parent = nullptr);

        ~EditReadDiscardAction();

        QAction *getDiscardAction() const;

    signals:
        void triggered(Action p_act);

    private:
        QScopedPointer<QMenu> m_readMenu;

        QAction *m_discardAct = nullptr;
    };
}

#endif // EDITREADDISCARDACTION_H
