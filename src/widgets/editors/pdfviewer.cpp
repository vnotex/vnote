#include "pdfviewer.h"

#include <QWebChannel>
#include <QContextMenuEvent>
#include <QMenu>
#include <QApplication>
#include <QMimeData>
#include <QScopedPointer>

#include "pdfvieweradapter.h"
#include "previewhelper.h"
#include <utils/clipboardutils.h>
#include <utils/fileutils.h>
#include <utils/utils.h>
#include <utils/widgetutils.h>
#include <core/configmgr.h>
#include <core/editorconfig.h>
#include "../widgetsfactory.h"
#include "../viewwindow.h"

using namespace vnotex;

PdfViewer::PdfViewer(PdfViewerAdapter *p_adapter,
                               const QColor &p_background,
                               qreal p_zoomFactor,
                               QWidget *p_parent)
    : PdfViewer(p_adapter, nullptr, p_background, p_zoomFactor, p_parent)
{
}

PdfViewer::PdfViewer(PdfViewerAdapter *p_adapter,
                               const ViewWindow *p_viewWindow,
                               const QColor &p_background,
                               qreal p_zoomFactor,
                               QWidget *p_parent)
    : WebViewer(p_background, p_zoomFactor, p_parent),
      m_adapter(p_adapter),
      m_viewWindow(p_viewWindow)
{
    m_adapter->setParent(this);

    auto channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("vxAdapter"), m_adapter);

    page()->setWebChannel(channel);

    qDebug() << "---------- readx pdf ------" << m_adapter;
}

PdfViewerAdapter *PdfViewer::adapter() const
{
    return m_adapter;
}

void PdfViewer::contextMenuEvent(QContextMenuEvent *p_event)
{
     qDebug() << "---------- event ------";
}
