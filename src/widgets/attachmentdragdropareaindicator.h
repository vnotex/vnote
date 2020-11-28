#ifndef ATTACHMENTDRAGDROPAREAINDICATOR_H
#define ATTACHMENTDRAGDROPAREAINDICATOR_H

#include "dragdropareaindicator.h"

namespace vnotex
{
    class ViewWindow;

    class AttachmentDragDropAreaIndicator : public DragDropAreaIndicatorInterface
    {
    public:
        AttachmentDragDropAreaIndicator(ViewWindow *p_viewWindow);

        bool handleDragEnterEvent(QDragEnterEvent *p_event) Q_DECL_OVERRIDE;

        bool handleDropEvent(QDropEvent *p_event) Q_DECL_OVERRIDE;

        static bool isAccepted(const QDragEnterEvent *p_event);

    private:
        ViewWindow *m_viewWindow = nullptr;
    };
}

#endif // ATTACHMENTDRAGDROPAREAINDICATOR_H
