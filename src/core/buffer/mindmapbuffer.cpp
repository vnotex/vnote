#include "mindmapbuffer.h"

#include <widgets/mindmapviewwindow.h>

using namespace vnotex;

MindMapBuffer::MindMapBuffer(const BufferParameters &p_parameters,
                             QObject *p_parent)
    : Buffer(p_parameters, p_parent)
{
}

ViewWindow *MindMapBuffer::createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent)
{
    Q_UNUSED(p_paras);
    return new MindMapViewWindow(p_parent);
}
