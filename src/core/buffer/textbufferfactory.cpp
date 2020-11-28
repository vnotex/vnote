#include "textbufferfactory.h"

#include "textbuffer.h"

using namespace vnotex;

Buffer *TextBufferFactory::createBuffer(const BufferParameters &p_parameters,
                                        QObject *p_parent)
{
    return new TextBuffer(p_parameters, p_parent);
}
