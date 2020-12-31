#include "biaction.h"

#include <QMenu>
#include <QDebug>
#include <QToolButton>

#include <utils/widgetutils.h>

using namespace vnotex;

BiAction::BiAction(const QIcon &p_icon,
                   const QString &p_text,
                   const QIcon &p_altIcon,
                   const QString &p_altText,
                   QObject *p_parent)
    : QAction(p_icon, p_text, p_parent)
{
    m_resources[State::Default].m_icon = p_icon;
    m_resources[State::Default].m_text = p_text;

    m_resources[State::Alternative].m_icon = p_altIcon;
    m_resources[State::Alternative].m_text = p_altText;

    connect(this, &QAction::triggered,
            this, [this]() {
                toggleState();
            });
}

BiAction::State BiAction::previousState() const
{
    if (m_state == State::Default) {
        return State::Alternative;
    } else {
        return State::Default;
    }
}

BiAction::State BiAction::state() const
{
    return m_state;
}

void BiAction::setState(BiAction::State p_state)
{
    if (p_state == m_state) {
        return;
    }

    const QString preText = m_resources[m_state].m_text;
    auto preMenu = m_resources[m_state].m_menu;

    m_state = p_state;

    auto &resource = m_resources[m_state];
    if (!resource.m_icon.isNull()) {
        setIcon(resource.m_icon);
    }

    // Use replacement instead since there may exist shortcut text.
    setText(text().replace(preText, resource.m_text));

    setMenu(resource.m_menu);
    if (resource.m_menu) {
        resource.m_menu->setEnabled(true);
    }

    if (preMenu) {
        preMenu->setEnabled(false);
    }

    updateToolButtonPopupMode();
}

void BiAction::setStateMenu(BiAction::State p_state, QMenu *p_menu)
{
    m_resources[p_state].m_menu = p_menu;
    if (m_state == p_state) {
        setMenu(m_resources[m_state].m_menu);
        updateToolButtonPopupMode();
    }
}

void BiAction::toggleState()
{
    setState(previousState());
}

void BiAction::setToolButtonForAction(QToolButton *p_btn)
{
    m_toolBtn = p_btn;
    updateToolButtonPopupMode();
}

void BiAction::updateToolButtonPopupMode()
{
    if (m_toolBtn) {
        m_toolBtn->setPopupMode(menu() ? QToolButton::MenuButtonPopup
                                       : QToolButton::DelayedPopup);
        WidgetUtils::updateStyle(m_toolBtn);
    }
}
