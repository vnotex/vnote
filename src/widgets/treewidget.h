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
            ClickSpaceToClearSelection = 0x1,
            EnhancedStyle = 0x2
        };
        Q_DECLARE_FLAGS(Flags, Flag)

        explicit TreeWidget(QWidget *p_parent = nullptr);

        TreeWidget(TreeWidget::Flags p_flags, QWidget *p_parent = nullptr);

        void mark(QTreeWidgetItem *p_item, int p_column);

        void unmark(QTreeWidgetItem *p_item, int p_column);

        static void setupSingleColumnHeaderlessTree(QTreeWidget *p_widget, bool p_contextMenu, bool p_extendedSelection);

        static void showHorizontalScrollbar(QTreeWidget *p_tree);

        static QTreeWidgetItem *findItem(const QTreeWidget *p_widget, const QVariant &p_data, int p_column = 0);

        // Next visible item.
        static QTreeWidgetItem *nextItem(const QTreeWidget* p_tree,
                                         QTreeWidgetItem *p_item,
                                         bool p_forward);

        static QVector<QTreeWidgetItem *> getVisibleItems(const QTreeWidget *p_widget);

        // @p_func: return false to abort the iteration.
        static void forEachItem(const QTreeWidget *p_widget, const std::function<bool(QTreeWidgetItem *p_item)> &p_func);

        static void expandRecursively(QTreeWidgetItem *p_item);

        static void selectParentItem(QTreeWidget *p_widget);

        static bool isExpanded(const QTreeWidget *p_widget);

    signals:
        // Emit when single item is selected and Drag&Drop to move internally.
        void itemMoved(QTreeWidgetItem *p_item);

    protected:
        void mousePressEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

        void dropEvent(QDropEvent *p_event) Q_DECL_OVERRIDE;

    private:
        static QTreeWidgetItem *findItemHelper(QTreeWidgetItem *p_item, const QVariant &p_data, int p_column);

        static QTreeWidgetItem *nextSibling(const QTreeWidget *p_widget,
                                            QTreeWidgetItem *p_item,
                                            bool p_forward);

        static QTreeWidgetItem *lastItemOfTree(QTreeWidgetItem *p_item);

        // @p_func: return false to abort the iteration.
        // Return false to abort the ieration.
        static bool forEachItem(QTreeWidgetItem *p_item, const std::function<bool(QTreeWidgetItem *p_item)> &p_func);

        Flags m_flags = Flag::None;
    };

    Q_DECLARE_OPERATORS_FOR_FLAGS(TreeWidget::Flags)
} // ns vnotex

#endif // TREEWIDGET_H
