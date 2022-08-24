#ifndef TAGPOPUP_H
#define TAGPOPUP_H

#include "buttonpopup.h"

class QToolButton;

namespace vnotex
{
    class Buffer;
    class TagViewer;

    class TagPopup : public ButtonPopup
    {
        Q_OBJECT
    public:
        TagPopup(QToolButton *p_btn, QWidget *p_parent = nullptr);

        void setBuffer(Buffer *p_buffer);

    private:
        void setupUI();

        Buffer *m_buffer = nullptr;

        // Managed by QObject.
        TagViewer *m_tagViewer = nullptr;
    };
}

#endif // TAGPOPUP_H
