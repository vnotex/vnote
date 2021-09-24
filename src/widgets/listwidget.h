#ifndef LISTWIDGET_H
#define LISTWIDGET_H

#include <QListWidget>

#include <functional>

#include <QVector>

namespace vnotex
{
    class ListWidget : public QListWidget
    {
        Q_OBJECT
    public:
        explicit ListWidget(QWidget *p_parent = nullptr);

        ListWidget(bool p_enhancedStyle, QWidget *p_parent = nullptr);

        static QVector<QListWidgetItem *> getVisibleItems(const QListWidget *p_widget);

        static QListWidgetItem *createSeparatorItem(const QString &p_text);

        static bool isSeparatorItem(const QListWidgetItem *p_item);

        static QListWidgetItem *findItem(const QListWidget *p_widget, const QVariant &p_data);

        // @p_func: return false to abort the iteration.
        static void forEachItem(const QListWidget *p_widget, const std::function<bool(QListWidgetItem *p_item)> &p_func);

    protected:
        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    private:
        enum
        {
            ItemTypeSeparator = 2000
        };

        void initialize();

        static QBrush s_separatorForeground;

        static QBrush s_separatorBackground;
    };
}

#endif // LISTWIDGET_H
