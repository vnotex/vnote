#include "listwidget.h"

#include <QKeyEvent>
#include <utils/widgetutils.h>

using namespace vnotex;

ListWidget::ListWidget(QWidget *p_parent)
    : QListWidget(p_parent)
{
}

void ListWidget::keyPressEvent(QKeyEvent *p_event)
{
    if (WidgetUtils::processKeyEventLikeVi(this, p_event)) {
        return;
    }

    QListWidget::keyPressEvent(p_event);
}

QVector<QListWidgetItem *> ListWidget::getVisibleItems(const QListWidget *p_widget)
{
    QVector<QListWidgetItem *> items;

    auto firstItem = p_widget->itemAt(0, 0);
    if (!firstItem) {
        return items;
    }

    auto lastItem = p_widget->itemAt(p_widget->viewport()->rect().bottomLeft());

    int firstRow = p_widget->row(firstItem);
    int lastRow = lastItem ? p_widget->row(lastItem) : (p_widget->count() - 1);
    for (int i = firstRow; i <= lastRow; ++i) {
        auto item = p_widget->item(i);
        if (!item->isHidden() && item->flags() != Qt::NoItemFlags) {
            items.append(item);
        }
    }

    return items;
}
