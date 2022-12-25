#ifndef MINDMAPBUFFERFACTORY_H
#define MINDMAPBUFFERFACTORY_H

#include "ibufferfactory.h"

namespace vnotex
{
    // Buffer factory for MindMap file.
    class MindMapBufferFactory : public IBufferFactory
    {
    public:
        Buffer *createBuffer(const BufferParameters &p_parameters,
                             QObject *p_parent) Q_DECL_OVERRIDE;

        bool isBufferCreatedByFactory(const Buffer *p_buffer) const Q_DECL_OVERRIDE;
    };
}

#endif // MINDMAPBUFFERFACTORY_H
