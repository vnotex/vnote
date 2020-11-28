#ifndef LISTWIDGET_H
#define LISTWIDGET_H

#include <QListWidget>
#include <QVector>

namespace vnotex
{
    class ListWidget : public QListWidget
    {
        Q_OBJECT
    public:
        explicit ListWidget(QWidget *p_parent = nullptr);

        static QVector<QListWidgetItem *> getVisibleItems(const QListWidget *p_widget);

    protected:
        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;
    };
}

#endif // LISTWIDGET_H
