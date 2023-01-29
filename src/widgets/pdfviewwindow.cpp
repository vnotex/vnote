#include "pdfviewwindow.h"

#include <core/vnotex.h>
#include <core/thememgr.h>
#include <core/htmltemplatehelper.h>
#include <core/configmgr.h>
#include <core/editorconfig.h>
#include <core/pdfviewerconfig.h>
#include <utils/pathutils.h>

#include "editors/pdfviewer.h"
#include "editors/pdfvieweradapter.h"

using namespace vnotex;

PdfViewWindow::PdfViewWindow(QWidget *p_parent)
    : ViewWindow(p_parent)
{
    m_mode = ViewWindowMode::Read;
    setupUI();
}

void PdfViewWindow::setupUI()
{
    setupViewer();
    setCentralWidget(m_viewer);

    setupToolBar();
}

void PdfViewWindow::setupToolBar()
{
    auto toolBar = createToolBar(this);
    addToolBar(toolBar);

    addAction(toolBar, ViewWindowToolBarHelper::Tag);
}

void PdfViewWindow::setupViewer()
{
    Q_ASSERT(!m_viewer);

    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &pdfViewerConfig = editorConfig.getPdfViewerConfig();

    updateConfigRevision();

    HtmlTemplateHelper::updatePdfViewerTemplate(pdfViewerConfig);

    auto adapter = new PdfViewerAdapter(nullptr);
    m_viewer = new PdfViewer(adapter,
                             VNoteX::getInst().getThemeMgr().getBaseBackground(),
                             1.0,
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
    Q_UNUSED(p_mode);
    Q_ASSERT(false);
}

void PdfViewWindow::openTwice(const QSharedPointer<FileOpenParameters> &p_paras)
{
    Q_UNUSED(p_paras);
}

ViewWindowSession PdfViewWindow::saveSession() const
{
    auto session = ViewWindow::saveSession();
    return session;
}

void PdfViewWindow::applySnippet(const QString &p_name)
{
    Q_UNUSED(p_name);
}

void PdfViewWindow::applySnippet()
{
}

void PdfViewWindow::fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const
{
    Q_UNUSED(p_callback);
}

void PdfViewWindow::handleEditorConfigChange()
{
    if (updateConfigRevision()) {
        const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
        const auto &pdfViewerConfig = editorConfig.getPdfViewerConfig();

        HtmlTemplateHelper::updatePdfViewerTemplate(pdfViewerConfig);
    }
}

void PdfViewWindow::setModified(bool p_modified)
{
    Q_UNUSED(p_modified);
}

void PdfViewWindow::print()
{
}

void PdfViewWindow::syncEditorFromBuffer()
{
    auto buffer = getBuffer();
    if (buffer) {
        const auto url = PathUtils::pathToUrl(buffer->getContentPath());
        // Solution to ASCII problems, like these file names with these symbols # + &.
        const auto urlStr = QUrl::toPercentEncoding(url.toString(QUrl::FullyDecoded));
        auto templateUrl = PathUtils::pathToUrl(HtmlTemplateHelper::getPdfViewerTemplatePath());
        templateUrl.setQuery("file=" + urlStr);
        m_viewer->setHtml(HtmlTemplateHelper::getPdfViewerTemplate(), templateUrl);
    } else {
        m_viewer->setHtml("");
    }
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
    Q_UNUSED(p_zoomIn);
}

PdfViewerAdapter *PdfViewWindow::adapter() const
{
    if (m_viewer) {
        return dynamic_cast<PdfViewerAdapter *>(m_viewer->adapter());
    }

    return nullptr;
}

bool PdfViewWindow::updateConfigRevision()
{
    bool changed = false;

    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    if (m_editorConfigRevision != editorConfig.revision()) {
        changed = true;
        m_editorConfigRevision = editorConfig.revision();
    }

    if (m_viewerConfigRevision != editorConfig.getPdfViewerConfig().revision()) {
        changed = true;
        m_viewerConfigRevision = editorConfig.getPdfViewerConfig().revision();
    }

    return changed;
}
