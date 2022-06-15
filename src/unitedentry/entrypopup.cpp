#include "entrypopup.h"

#include <QVBoxLayout>
#include <QDebug>
#include <QWindow>

using namespace vnotex;

EntryPopup::EntryPopup(QWidget *p_parent)
    : QFrame(p_parent)
{
    Q_ASSERT(p_parent);
    auto layout = new QVBoxLayout(this);
    Q_UNUSED(layout);
}

EntryPopup::~EntryPopup()
{
    if (m_widget) {
        takeWidget(m_widget.data());
    }
}

void EntryPopup::setWidget(const QSharedPointer<QWidget> &p_widget)
{
    if (p_widget == m_widget) {
        return;
    }

    if (m_widget) {
        takeWidget(m_widget.data());
    }

    m_widget = p_widget;
    if (m_widget) {
        layout()->addWidget(m_widget.data());
        m_widget->show();
    }
}

void EntryPopup::takeWidget(QWidget *p_widget)
{
    layout()->removeWidget(p_widget);
    p_widget->hide();
    p_widget->setParent(nullptr);
}
