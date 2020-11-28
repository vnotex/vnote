#ifndef TREEVIEW_H
#define TREEVIEW_H

#include <QTreeView>
#include <QVariant>

namespace vnotex
{
    class TreeView : public QTreeView
    {
        Q_OBJECT
    public:
        explicit TreeView(QWidget *p_parent = nullptr);

    protected:
        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;
    };
}

#endif // TREEVIEW_H
