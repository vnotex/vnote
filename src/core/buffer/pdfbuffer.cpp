#include "pdfbuffer.h"

#include <widgets/pdfviewwindow.h>

using namespace vnotex;

PdfBuffer::PdfBuffer(const BufferParameters &p_parameters,
                     QObject *p_parent)
    : Buffer(p_parameters, p_parent)
{
}

ViewWindow *PdfBuffer::createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent)
{
    Q_UNUSED(p_paras);
    return new PdfViewWindow(p_parent);
}
