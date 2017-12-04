#include "vbuttonmenuitem.h"

#include <QAction>

VButtonMenuItem::VButtonMenuItem(QAction *p_action, QWidget *p_parent)
    : QPushButton(p_parent), m_action(p_action)
{
    init();
}

VButtonMenuItem::VButtonMenuItem(QAction *p_action, const QString &p_text, QWidget *p_parent)
    : QPushButton(p_text, p_parent), m_action(p_action)
{
    init();
}

void VButtonMenuItem::init()
{
    connect(this, &QPushButton::clicked,
            m_action, &QAction::triggered);
}
