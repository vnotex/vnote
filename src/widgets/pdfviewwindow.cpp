#include "pdfviewwindow.h"

#include <QTextDocument>
#include <QDebug>
#include <QScrollBar>
#include <QToolBar>
#include <QPrinter>

#include <vtextedit/vtextedit.h>
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

PdfViewWindow::~PdfViewWindow()
{

}

void PdfViewWindow::setupUI()
{

}

void PdfViewWindow::handleEditorConfigChange()
{
//    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
//    const auto &textEditorConfig = editorConfig.getTextEditorConfig();

    // TODO
}
