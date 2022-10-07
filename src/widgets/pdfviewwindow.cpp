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

//#include <vtextedit/vtextedit.h>
#include <core/editorconfig.h>
#include <core/coreconfig.h>

//#include "textviewwindowhelper.h"
//#include "toolbarhelper.h"
//#include "editors/texteditor.h"
#include <core/vnotex.h>
#include <core/thememgr.h>
//#include "editors/statuswidget.h"
#include <core/fileopenparameters.h>
#include <utils/printutils.h>

using namespace vnotex;

PdfViewWindow::PdfViewWindow(QWidget *p_parent)
    : ViewWindow(p_parent)
{
    m_mode = ViewWindowMode::Read;
    setupUI();
}

//PdfViewWindow::~PdfViewWindow()
//{
//    // TODO
//}

void PdfViewWindow::setupUI()
{
    // Central widget.
    m_splitter = new QSplitter(this);
    m_splitter->setContentsMargins(0, 0, 0, 0);
    setCentralWidget(m_splitter);
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
    Q_UNUSED(p_mode);
    Q_ASSERT(false);
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
    qDebug() << "hello";
}

void PdfViewWindow::setModified(bool p_modified)
{
}

void PdfViewWindow::handleBufferChangedInternal(const QSharedPointer<FileOpenParameters> &p_paras)
{
}

void PdfViewWindow::print()
{
   qDebug() << "print";
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

