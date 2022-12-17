#ifndef PDFBUFFERFACTORY_H
#define PDFBUFFERFACTORY_H

#include "ibufferfactory.h"

namespace vnotex
{
    // Buffer factory for Pdf file.
    class PdfBufferFactory : public IBufferFactory
    {
    public:
        Buffer *createBuffer(const BufferParameters &p_parameters,
                             QObject *p_parent) Q_DECL_OVERRIDE;

        bool isBufferCreatedByFactory(const Buffer *p_buffer) const Q_DECL_OVERRIDE;
    };
} // vnotex

#endif // PDFBUFFERFACTORY_H
