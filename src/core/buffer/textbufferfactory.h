#ifndef TEXTBUFFERFACTORY_H
#define TEXTBUFFERFACTORY_H

#include "ibufferfactory.h"

namespace vnotex
{
    // Buffer factory for text file.
    class TextBufferFactory : public IBufferFactory
    {
    public:
        Buffer *createBuffer(const BufferParameters &p_parameters,
                             QObject *p_parent) Q_DECL_OVERRIDE;
    };
}

#endif // TEXTBUFFERFACTORY_H
