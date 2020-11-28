#ifndef EDITORMARKDOWNVIEWERADAPTER_H
#define EDITORMARKDOWNVIEWERADAPTER_H

#include "markdownvieweradapter.h"

namespace vnotex
{
    class Buffer;

    class EditorMarkdownViewerAdapter : public MarkdownViewerAdapter
    {
        Q_OBJECT
    public:
        EditorMarkdownViewerAdapter(Buffer *p_buffer, QObject *p_parent = nullptr);

        void setBuffer(Buffer *p_buffer);

    public slots:

    private:
        Buffer *m_buffer = nullptr;
    };
}

#endif // EDITORMARKDOWNVIEWERADAPTER_H
