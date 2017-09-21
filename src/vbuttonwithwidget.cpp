#include "vbuttonwithwidget.h"

#include <QMenu>

VButtonWithWidget::VButtonWithWidget(QWidget *p_widget,
                                     QWidget *p_parent)
    : QPushButton(p_parent), m_popupWidget(p_widget)
{
    init();
}

VButtonWithWidget::VButtonWithWidget(const QString &p_text,
                                     QWidget *p_widget,
                                     QWidget *p_parent)
    : QPushButton(p_text, p_parent), m_popupWidget(p_widget)
{
    init();
}

VButtonWithWidget::VButtonWithWidget(const QIcon &p_icon,
                                     const QString &p_text,
                                     QWidget *p_widget,
                                     QWidget *p_parent)
    : QPushButton(p_icon, p_text, p_parent), m_popupWidget(p_widget)
{
    init();
}

void VButtonWithWidget::init()
{
    m_popupWidget->setParent(this);

    QMenu *menu = new QMenu(this);
    VButtonWidgetAction *act = new VButtonWidgetAction(m_popupWidget, menu);
    menu->addAction(act);
    connect(menu, &QMenu::aboutToShow,
            this, [this]() {
                emit popupWidgetAboutToShow(m_popupWidget);
            });

    setMenu(menu);
}

QWidget *VButtonWithWidget::getPopupWidget() const
{
    return m_popupWidget;
}

void VButtonWithWidget::showPopupWidget()
{
    showMenu();
}
