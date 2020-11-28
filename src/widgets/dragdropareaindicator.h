#ifndef DRAGDROPAREAINDICATOR_H
#define DRAGDROPAREAINDICATOR_H

#include <QFrame>

namespace vnotex
{
    class DragDropAreaIndicatorInterface
    {
    public:
        virtual ~DragDropAreaIndicatorInterface() {}

        virtual bool handleDragEnterEvent(QDragEnterEvent *p_event) = 0;

        virtual bool handleDropEvent(QDropEvent *p_event) = 0;
    };

    class DragDropAreaIndicator : public QFrame
    {
        Q_OBJECT
    public:
        DragDropAreaIndicator(DragDropAreaIndicatorInterface *p_interface,
                              const QString &p_text,
                              QWidget *p_parent = nullptr);

    protected:
        // To accept specific drop.
        void dragEnterEvent(QDragEnterEvent *p_event) Q_DECL_OVERRIDE;

        // Drop the data.
        void dropEvent(QDropEvent *p_event) Q_DECL_OVERRIDE;

        void mouseReleaseEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

    private:
        void setupUI(const QString &p_text);

        DragDropAreaIndicatorInterface *m_interface = nullptr;
    };
}

#endif // DRAGDROPAREAINDICATOR_H
