#include "treeview.h"

#include <QKeyEvent>

#include <utils/widgetutils.h>

using namespace vnotex;

TreeView::TreeView(QWidget *p_parent)
    : QTreeView(p_parent)
{
}

void TreeView::keyPressEvent(QKeyEvent *p_event)
{
    if (WidgetUtils::processKeyEventLikeVi(this, p_event)) {
        return;
    }

    QTreeView::keyPressEvent(p_event);
}
