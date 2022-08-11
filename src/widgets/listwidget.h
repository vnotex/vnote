#ifndef LISTWIDGET_H
#define LISTWIDGET_H

#include <QListWidget>

#include <functional>

#include <QVector>

class QTimer;

namespace vnotex
{
    class ListWidget : public QListWidget
    {
        Q_OBJECT
    public:
        enum ActivateReason
        {
            SingleClick = 0,
            DoubleClick,
            Button
        };

        explicit ListWidget(QWidget *p_parent = nullptr);

        ListWidget(bool p_enhancedStyle, QWidget *p_parent = nullptr);

        static QVector<QListWidgetItem *> getVisibleItems(const QListWidget *p_widget);

        static QListWidgetItem *createSeparatorItem(const QString &p_text);

        static bool isSeparatorItem(const QListWidgetItem *p_item);

        static QListWidgetItem *findItem(const QListWidget *p_widget, const QVariant &p_data);

        // @p_func: return false to abort the iteration.
        static void forEachItem(const QListWidget *p_widget, const std::function<bool(QListWidgetItem *p_item)> &p_func);

    signals:
        // Item activated not by mouse clicking.
        void itemActivatedPlus(QListWidgetItem *p_item, ActivateReason p_source);

    protected:
        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    private:
        enum
        {
            ItemTypeSeparator = 2000
        };

        void handleItemClick();

        QTimer *m_clickTimer = nullptr;

        QListWidgetItem *m_clickedItem = nullptr;

        bool m_isDoubleClick = false;
    };
}

#endif // LISTWIDGET_H
