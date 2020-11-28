#include "textbuffer.h"

#include <widgets/textviewwindow.h>

using namespace vnotex;

TextBuffer::TextBuffer(const BufferParameters &p_parameters,
                       QObject *p_parent)
    : Buffer(p_parameters, p_parent)
{
}

ViewWindow *TextBuffer::createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent)
{
    Q_UNUSED(p_paras);
    return new TextViewWindow(p_parent);
}
