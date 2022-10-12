#include "pdfviewwindow.h"

#include <QSplitter>
#include <QToolBar>
#include <QStackedWidget>
#include <QEvent>
#include <QCoreApplication>
#include <QScrollBar>
#include <QLabel>
#include <QApplication>
#include <QProgressDialog>
#include <QMenu>
#include <QActionGroup>
#include <QTimer>
#include <QPrinter>

#include <core/fileopenparameters.h>
#include <core/editorconfig.h>
#include <core/coreconfig.h>
#include <core/htmltemplatehelper.h>
#include <core/exception.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/pegmarkdownhighlighter.h>
#include <vtextedit/markdowneditorconfig.h>
#include <utils/pathutils.h>
#include <utils/widgetutils.h>
#include <utils/printutils.h>
#include <utils/fileutils.h>
#include <buffer/markdownbuffer.h>
#include <core/vnotex.h>
#include <core/thememgr.h>
#include <imagehost/imagehostmgr.h>
#include <imagehost/imagehost.h>
#include "editors/markdowneditor.h"
#include "textviewwindowhelper.h"
#include "editors/markdownviewer.h"
#include "editors/editormarkdownvieweradapter.h"
#include "editors/previewhelper.h"
#include "dialogs/deleteconfirmdialog.h"
#include "outlineprovider.h"
#include "toolbarhelper.h"
#include "findandreplacewidget.h"
#include "editors/statuswidget.h"
#include "editors/plantumlhelper.h"
#include "editors/graphvizhelper.h"
#include "messageboxhelper.h"

#include "editors/pdfviewer.h"
#include "editors/pdfvieweradapter.h"

using namespace vnotex;

PdfViewWindow::PdfViewWindow(QWidget *p_parent)
    : ViewWindow(p_parent)
{
    m_mode = ViewWindowMode::Read;
    setupUI();
    qDebug() << "------ pdf view window ------";
}

//PdfViewWindow::~PdfViewWindow()
//{
//    // TODO
//}

void PdfViewWindow::setupUI()
{
}

void PdfViewWindow::setupViewer()
{
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &markdownEditorConfig = editorConfig.getMarkdownEditorConfig();

    HtmlTemplateHelper::updateMarkdownViewerTemplate(markdownEditorConfig);

    auto adapter = new PdfViewerAdapter(nullptr);
    m_viewer = new PdfViewer(adapter,
                             this,
                             VNoteX::getInst().getThemeMgr().getBaseBackground(),
                             markdownEditorConfig.getZoomFactorInReadMode(),
                             this);
}

QString PdfViewWindow::getLatestContent() const
{
    return NULL;
}

QString PdfViewWindow::selectedText() const
{
    return QString();
}

void PdfViewWindow::setMode(ViewWindowMode p_mode)
{
}

void PdfViewWindow::openTwice(const QSharedPointer<FileOpenParameters> &p_paras)
{

}

ViewWindowSession PdfViewWindow::saveSession() const
{
    auto session = ViewWindow::saveSession();
    session.m_lineNumber = 1;
    return session;
}

void PdfViewWindow::applySnippet(const QString &p_name)
{
}

void PdfViewWindow::applySnippet()
{
}

void PdfViewWindow::fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const
{
}

void PdfViewWindow::handleEditorConfigChange()
{
}

void PdfViewWindow::setModified(bool p_modified)
{
}

void PdfViewWindow::handleBufferChangedInternal(const QSharedPointer<FileOpenParameters> &p_paras)
{
    qDebug() << "------ start build pdf from buffer --------";
    setModeInternal(false);
}

void PdfViewWindow::setModeInternal(bool p_syncBuffer)
{
    qDebug() << "------ 1 --------";
    setupViewer();
    syncViewerFromBuffer();

    qDebug() << "------ 3 --------";
    // Avoid focus glitch.
    m_viewer->show();
    m_viewer->setFocus();


//    if (p_mode == m_mode) {
//        return;
//    }

//    m_mode = p_mode;
//    switch (m_mode) {
//    case ViewWindowMode::Read:
//    case ViewWindowMode::Edit:
//    {
//        setupViewer();
//        syncViewerFromBuffer();

//        qDebug() << "------ 3 --------";
//        // Avoid focus glitch.
//        m_viewer->show();
//        m_viewer->setFocus();
//        break;
//    }
//    default:
//        Q_ASSERT(false);
//        break;
//    }
}

void PdfViewWindow::print()
{
}

void PdfViewWindow::syncEditorFromBuffer()
{
}

void PdfViewWindow::syncEditorFromBufferContent()
{
}

void PdfViewWindow::scrollUp()
{
}

void PdfViewWindow::scrollDown()
{
}

void PdfViewWindow::zoom(bool p_zoomIn)
{
}

void PdfViewWindow::syncViewerFromBuffer()
{
//    if (!m_viewer) {
//        return;
//    }

    auto buffer = getBuffer();

    qDebug() << "------ 2 --------";
    // adapter()->reset();
    m_viewer->setHtml(HtmlTemplateHelper::getMarkdownViewerTemplate(),
                      PathUtils::pathToUrl(buffer->getContentPath()));
    adapter()->setText(m_bufferRevision, buffer->getContent(), 1);
}


void PdfViewWindow::syncViewerFromBufferContent()
{
}

PdfViewerAdapter *PdfViewWindow::adapter() const
{
    if (m_viewer) {
        return dynamic_cast<PdfViewerAdapter *>(m_viewer->adapter());
    }

    return nullptr;
}
