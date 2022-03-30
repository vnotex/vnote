#include "entrywidgetfactory.h"

#include <QLabel>

#include <widgets/widgetsfactory.h>
#include <widgets/treewidget.h>

using namespace vnotex;

QSharedPointer<QTreeWidget> EntryWidgetFactory::createTreeWidget(int p_columnCount)
{
    auto tree = QSharedPointer<TreeWidget>::create(TreeWidget::Flag::EnhancedStyle, nullptr);
    tree->setColumnCount(p_columnCount);
    tree->setHeaderHidden(true);
    TreeWidget::showHorizontalScrollbar(tree.data());
    return tree;
}

QSharedPointer<QLabel> EntryWidgetFactory::createLabel(const QString &p_info)
{
    auto label = QSharedPointer<QLabel>::create(p_info, nullptr);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto fnt = label->font();
    fnt.setPointSize(fnt.pointSize() + 2);
    label->setFont(fnt);

    return label;
}
