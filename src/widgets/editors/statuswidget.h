#ifndef STATUSWIDGET_H
#define STATUSWIDGET_H

#include <QWidget>
#include <QSharedPointer>

class QLabel;
class QTimer;

namespace vnotex
{
    // A status widget wrapper for ViewWindow.
    class StatusWidget : public QWidget
    {
        Q_OBJECT
    public:
        explicit StatusWidget(QWidget *p_parent = nullptr);

        ~StatusWidget();

        void showMessage(const QString &p_msg, int p_milliseconds = 3000);

        void setEditorStatusWidget(const QSharedPointer<QWidget> &p_editorWidget);

    private:
        QLabel *m_messageLabel = nullptr;

        QTimer *m_messageTimer = nullptr;

        // Use shared pointer here to hold the widget.
        // Should detach the widget in destructor.
        QSharedPointer<QWidget> m_editorWidget;
    };
}

#endif // STATUSWIDGET_H
