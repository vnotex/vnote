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

    setWindowFlags(Qt::ToolTip);
    setFocusPolicy(Qt::FocusPolicy::ClickFocus);
}

EntryPopup::~EntryPopup()
{
    if (m_widget) {
        takeWidget(m_widget.data());
    }
}

void EntryPopup::setWidget(const QSharedPointer<QWidget> &p_widget)
{
    Q_ASSERT(p_widget);

    if (p_widget == m_widget) {
        return;
    }

    if (m_widget) {
        takeWidget(m_widget.data());
    }

    layout()->addWidget(p_widget.data());
    m_widget = p_widget;
    m_widget->show();

    updateGeometryToContents();
}

void EntryPopup::takeWidget(QWidget *p_widget)
{
    layout()->removeWidget(p_widget);
    p_widget->hide();
    p_widget->setParent(nullptr);
}

void EntryPopup::showEvent(QShowEvent *p_event)
{
    QFrame::showEvent(p_event);

    updateGeometryToContents();
}

void EntryPopup::updateGeometryToContents()
{
    adjustSize();

    auto pa = parentWidget();
    auto pos = pa->mapToGlobal(QPoint(0, pa->height()));
    setGeometry(QRect(pos, preferredSize()));

    if (m_widget) {
        m_widget->updateGeometry();
    }
}

QSize EntryPopup::preferredSize() const
{
    const int minWidth = 400;
    const int minHeight = 300;

    auto pa = parentWidget();
    int w = pa->width();
    int h = sizeHint().height();
    if (auto win = pa->window()) {
        w = qMax(w, qMin(win->width() - 500, 900));
        h = qMax(h, qMin(win->height() - 500, 800));
    }
    return QSize(qMax(minWidth, w), qMax(h, minHeight));
}
