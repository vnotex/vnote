#ifndef SORTDIALOG_H
#define SORTDIALOG_H

#include "scrolldialog.h"

class QTreeWidget;
class QPushButton;

namespace vnotex
{
    class SortDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        SortDialog(const QString &p_title, const QString &p_info, QWidget *p_parent = nullptr);

        QTreeWidget *getTreeWidget() const;

        // Called after updating the QTreeWidget from getTreeWidget().
        void updateTreeWidget();

        // Get user data of column 0 from sorted items.
        QVector<QVariant> getSortedData() const;

    private:
        enum MoveOperation
        {
            Top,
            Up,
            Down,
            Bottom
        };

    private slots:
        void handleMoveOperation(MoveOperation p_op);

    private:
        void setupUI(const QString &p_title, const QString &p_info);

        QTreeWidget *m_treeWidget = nullptr;
    };
}

#endif // SORTDIALOG_H
