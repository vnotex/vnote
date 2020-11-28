#ifndef SELECTIONITEMWIDGET_H
#define SELECTIONITEMWIDGET_H

#include <QWidget>
#include <QVariant>

class QCheckBox;
class QLabel;

namespace vnotex
{
    // Tree/List item widget with checkbox.
    class SelectionItemWidget : public QWidget
    {
        Q_OBJECT
    public:
        SelectionItemWidget(const QIcon &p_icon,
                            const QString &p_text,
                            QWidget *p_parent = nullptr);

        SelectionItemWidget(const QString &p_text,
                            QWidget *p_parent = nullptr);

        bool isChecked() const;
        void setChecked(bool p_checked);

        const QVariant &getData() const;
        void setData(const QVariant &p_data);

        void setToolTip(const QString &p_tip);

        void setIcon(const QIcon &p_icon);

    signals:
        // Emit when item's check state changed.
        void checkStateChanged(int p_state);

    protected:
        void mouseDoubleClickEvent(QMouseEvent *p_event);

    private:
        void setupUI(const QString &p_text);

        QCheckBox *m_checkBox = nullptr;

        QLabel *m_iconLabel = nullptr;

        QLabel *m_textLabel = nullptr;

        QVariant m_data;
    };
}

#endif // SELECTIONITEMWIDGET_H
