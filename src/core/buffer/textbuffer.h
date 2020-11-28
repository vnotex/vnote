#ifndef TEXTBUFFER_H
#define TEXTBUFFER_H

#include "buffer.h"

namespace vnotex
{
    class TextBuffer : public Buffer
    {
        Q_OBJECT
    public:
        TextBuffer(const BufferParameters &p_parameters,
                   QObject *p_parent = nullptr);

    protected:
        ViewWindow *createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent) Q_DECL_OVERRIDE;
    };
}

#endif // TEXTBUFFER_H
