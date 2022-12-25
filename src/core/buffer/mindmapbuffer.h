#ifndef MINDMAPBUFFER_H
#define MINDMAPBUFFER_H

#include "buffer.h"

namespace vnotex
{
    class MindMapBuffer : public Buffer
    {
        Q_OBJECT
    public:
        MindMapBuffer(const BufferParameters &p_parameters,
                      QObject *p_parent = nullptr);

    protected:
        ViewWindow *createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras,
                                             QWidget *p_parent) Q_DECL_OVERRIDE;
    };
}

#endif // MINDMAPBUFFER_H
