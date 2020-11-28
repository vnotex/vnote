#include "editormarkdownvieweradapter.h"

#include <buffer/buffer.h>

using namespace vnotex;

EditorMarkdownViewerAdapter::EditorMarkdownViewerAdapter(Buffer *p_buffer,
                                                         QObject *p_parent)
    : MarkdownViewerAdapter(p_parent),
      m_buffer(p_buffer)
{
}

void EditorMarkdownViewerAdapter::setBuffer(Buffer *p_buffer)
{
    m_buffer = p_buffer;
}
