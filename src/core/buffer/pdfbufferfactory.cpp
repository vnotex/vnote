#include "pdfbufferfactory.h"

#include "pdfbuffer.h"
#include "urlbasedbufferprovider.h"

using namespace vnotex;

Buffer *PdfBufferFactory::createBuffer(const BufferParameters &p_parameters,
                                        QObject *p_parent)
{
    BufferParameters paras;
    paras.m_provider = QSharedPointer<UrlBasedBufferProvider>::create(p_parameters.m_provider);
    return new PdfBuffer(paras, p_parent);
}

bool PdfBufferFactory::isBufferCreatedByFactory(const Buffer *p_buffer) const
{
    return dynamic_cast<const PdfBuffer *>(p_buffer) != nullptr;
}
