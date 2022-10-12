#include "pdfbuffer.h"

#include <QDir>
#include <QDebug>

#include <widgets/markdownviewwindow.h>
#include <widgets/pdfviewwindow.h>
#include <notebook/node.h>
#include <utils/pathutils.h>
#include <buffer/bufferprovider.h>

using namespace vnotex;

PdfBuffer::PdfBuffer(const BufferParameters &p_parameters,
                     QObject *p_parent)
    : Buffer(p_parameters, p_parent)
{
}

ViewWindow *PdfBuffer::createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent)
{
    Q_UNUSED(p_paras);
    qDebug() << "------> start pdf";
    return new PdfViewWindow(p_parent);
}
