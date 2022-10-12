#include "pdfbufferfactory.h"

#include "pdfbuffer.h"

using namespace vnotex;

Buffer *PdfBufferFactory::createBuffer(const BufferParameters &p_parameters,
                                        QObject *p_parent)
{
    return new PdfBuffer(p_parameters, p_parent);
}

bool PdfBufferFactory::isBufferCreatedByFactory(const Buffer *p_buffer) const
{
    return dynamic_cast<const PdfBuffer *>(p_buffer) != nullptr;
}
