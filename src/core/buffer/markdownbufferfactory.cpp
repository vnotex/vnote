#include "markdownbufferfactory.h"

#include "markdownbuffer.h"

using namespace vnotex;

Buffer *MarkdownBufferFactory::createBuffer(const BufferParameters &p_parameters,
                                            QObject *p_parent)
{
    return new MarkdownBuffer(p_parameters, p_parent);
}

bool MarkdownBufferFactory::isBufferCreatedByFactory(const Buffer *p_buffer) const
{
    return dynamic_cast<const MarkdownBuffer *>(p_buffer) != nullptr;
}
