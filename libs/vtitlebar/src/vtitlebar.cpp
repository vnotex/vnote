#include "vtitlebar.h"

#include <QStyleOption>
#include <QPainter>

using namespace vnotex;

VTitleBar::VTitleBar(QWidget *p_parent)
    : QWidget(p_parent)
{

}

void VTitleBar::paintEvent(QPaintEvent *p_event)
{
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    QWidget::paintEvent(p_event);
}
