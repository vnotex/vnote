#include "dragdropareaindicator.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QDragEnterEvent>
#include <QDropEvent>

using namespace vnotex;

DragDropAreaIndicator::DragDropAreaIndicator(DragDropAreaIndicatorInterface *p_interface,
                                             const QString &p_text,
                                             QWidget *p_parent)
    : QFrame(p_parent),
      m_interface(p_interface)
{
    setupUI(p_text);

    setAcceptDrops(true);
}

void DragDropAreaIndicator::setupUI(const QString &p_text)
{
    auto mainLayout = new QHBoxLayout(this);

    auto label = new QLabel(p_text, this);
    label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    mainLayout->addWidget(label);
}

void DragDropAreaIndicator::dragEnterEvent(QDragEnterEvent *p_event)
{
    if (m_interface->handleDragEnterEvent(p_event)) {
        return;
    }
    QFrame::dragEnterEvent(p_event);
}

void DragDropAreaIndicator::dropEvent(QDropEvent *p_event)
{
    if (m_interface->handleDropEvent(p_event)) {
        hide();
        return;
    }
    QFrame::dropEvent(p_event);
}

void DragDropAreaIndicator::mouseReleaseEvent(QMouseEvent *p_event)
{
    QFrame::mouseReleaseEvent(p_event);
    hide();
}
