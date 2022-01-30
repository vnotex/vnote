#include "textbufferfactory.h"

#include "textbuffer.h"

using namespace vnotex;

Buffer *TextBufferFactory::createBuffer(const BufferParameters &p_parameters,
                                        QObject *p_parent)
{
    return new TextBuffer(p_parameters, p_parent);
}

bool TextBufferFactory::isBufferCreatedByFactory(const Buffer *p_buffer) const
{
    return dynamic_cast<const TextBuffer *>(p_buffer) != nullptr;
}
