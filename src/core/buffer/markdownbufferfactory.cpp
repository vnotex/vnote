#include "markdownbufferfactory.h"

#include "markdownbuffer.h"

using namespace vnotex;

Buffer *MarkdownBufferFactory::createBuffer(const BufferParameters &p_parameters,
                                            QObject *p_parent)
{
    return new MarkdownBuffer(p_parameters, p_parent);
}
