#include "editreaddiscardaction.h"

#include <QMenu>
#include <QDebug>

#include "widgetsfactory.h"

using namespace vnotex;

EditReadDiscardAction::EditReadDiscardAction(const QIcon &p_editIcon,
                                             const QString &p_editText,
                                             const QIcon &p_readIcon,
                                             const QString &p_readText,
                                             const QIcon &p_discardIcon,
                                             const QString &p_discardText,
                                             QObject *p_parent)
    : BiAction(p_editIcon,
               p_editText,
               p_readIcon,
               p_readText,
               p_parent)
{
    m_readMenu.reset(WidgetsFactory::createMenu(nullptr));
    m_discardAct = m_readMenu->addAction(p_discardIcon,
                                         p_discardText,
                                         m_readMenu.data(),
                                         [this]() {
                                             emit triggered(Action::Discard);
                                         });
    setStateMenu(State::Alternative, m_readMenu.data());

    connect(this, &BiAction::triggered,
            this, [this]() {
                switch (previousState()) {
                case State::Default:
                    emit triggered(Action::Edit);
                    break;

                case State::Alternative:
                    emit triggered(Action::Read);
                    break;

                default:
                    Q_ASSERT(false);
                    break;
                }
            });
}

EditReadDiscardAction::~EditReadDiscardAction()
{

}

QAction *EditReadDiscardAction::getDiscardAction() const
{
    return m_discardAct;
}
