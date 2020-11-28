#ifndef TREEWIDGET_H
#define TREEWIDGET_H

#include <QTreeWidget>
#include <QVariant>

namespace vnotex
{
    class TreeWidget : public QTreeWidget
    {
        Q_OBJECT
    public:
        enum Flag
        {
            None = 0,
            ClickSpaceToClearSelection = 0x1
        };
        Q_DECLARE_FLAGS(Flags, Flag)

        explicit TreeWidget(QWidget *p_parent = nullptr);

        TreeWidget(TreeWidget::Flags p_flags, QWidget *p_parent = nullptr);

        static void setupSingleColumnHeaderlessTree(QTreeWidget *p_widget, bool p_contextMenu, bool p_extendedSelection);

        static void showHorizontalScrollbar(QTreeWidget *p_tree);

        static QTreeWidgetItem *findItem(const QTreeWidget *p_widget, const QVariant &p_data);

        // Next visible item.
        static QTreeWidgetItem *nextItem(const QTreeWidget* p_tree,
                                         QTreeWidgetItem *p_item,
                                         bool p_forward);

        static QVector<QTreeWidgetItem *> getVisibleItems(const QTreeWidget *p_widget);

    protected:
        void mousePressEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    private:
        static QTreeWidgetItem *findItemHelper(QTreeWidgetItem *p_item, const QVariant &p_data);

        static QTreeWidgetItem *nextSibling(const QTreeWidget *p_widget,
                                            QTreeWidgetItem *p_item,
                                            bool p_forward);

        static QTreeWidgetItem *lastItemOfTree(QTreeWidgetItem *p_item);

        Flags m_flags = Flag::None;
    };

    Q_DECLARE_OPERATORS_FOR_FLAGS(TreeWidget::Flags)
} // ns vnotex

#endif // TREEWIDGET_H
