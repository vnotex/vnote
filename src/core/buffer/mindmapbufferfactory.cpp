#include "mindmapbufferfactory.h"

#include "mindmapbuffer.h"

using namespace vnotex;

Buffer *MindMapBufferFactory::createBuffer(const BufferParameters &p_parameters,
                                           QObject *p_parent)
{
    return new MindMapBuffer(p_parameters, p_parent);
}

bool MindMapBufferFactory::isBufferCreatedByFactory(const Buffer *p_buffer) const
{
    return dynamic_cast<const MindMapBuffer *>(p_buffer) != nullptr;
}
