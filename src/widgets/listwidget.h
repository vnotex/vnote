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

        static QListWidgetItem *createSeparatorItem(const QString &p_text);

        static bool isSeparatorItem(const QListWidgetItem *p_item);

    protected:
        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    private:
        static const int c_separatorType = 2000;
    };
}

#endif // LISTWIDGET_H
