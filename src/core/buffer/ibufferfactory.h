#ifndef IBUFFERFACTORY_H
#define IBUFFERFACTORY_H

#include <QSharedPointer>

namespace vnotex
{
    class Buffer;
    struct BufferParameters;

    // Abstract factory to create buffer.
    class IBufferFactory
    {
    public:
        virtual ~IBufferFactory()
        {
        }

        virtual Buffer *createBuffer(const BufferParameters &p_parameters,
                                     QObject *p_parent) = 0;
    };
} // ns vnotex

#endif // IBUFFERFACTORY_H
