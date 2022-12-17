#ifndef PDFBUFFER_H
#define PDFBUFFER_H

#include "buffer.h"

namespace vnotex
{
    class PdfBuffer : public Buffer
    {
        Q_OBJECT
    public:
        PdfBuffer(const BufferParameters &p_parameters,
                  QObject *p_parent = nullptr);

    protected:
        ViewWindow *createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras,
                                             QWidget *p_parent) Q_DECL_OVERRIDE;

    };
} // ns vnotex

#endif // PDFBUFFER_H
