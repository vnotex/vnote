#include "pdfviewwindow2.h"

#include <QToolBar>

#include <controllers/pdfviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/pdfviewerconfig.h>
#include <core/servicelocator.h>
#include <core/services/htmltemplateservice.h>
#include <gui/services/themeservice.h>

#include "editors/pdfviewer.h"
#include "editors/pdfvieweradapter.h"

using namespace vnotex;

PdfViewWindow2::PdfViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                               QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  m_controller = new PdfViewWindowController(p_services, this);
  m_mode = ViewWindowMode::Read;
  setupUI();
}

void PdfViewWindow2::setupUI() {
  setupViewer();
  setCentralWidget(m_viewer);

  setupToolBar();

  // Initial sync from buffer.
  syncEditorFromBuffer();
}

void PdfViewWindow2::setupToolBar() {
  auto *toolBar = createToolBar(this);
  addToolBar(toolBar);

  addAction(toolBar, ViewWindowToolBarHelper2::Attachment);

  // Common right-side actions: spacer + layout toggle + find-and-replace.
  addCommonToolBarActions(toolBar);
}

void PdfViewWindow2::setupViewer() {
  Q_ASSERT(!m_viewer);

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &pdfViewerConfig = editorConfig.getPdfViewerConfig();

  m_controller->checkAndUpdateConfigRevision();

  // Prepare the PDF.js HTML template via HtmlTemplateService (DI, not legacy singleton).
  auto *tmplService = getServices().get<HtmlTemplateService>();
  tmplService->updatePdfViewerTemplate(pdfViewerConfig);

  auto *themeService = getServices().get<ThemeService>();

  auto *pdfAdapter = new PdfViewerAdapter(nullptr);
  m_viewer = new PdfViewer(pdfAdapter, themeService->getBaseBackground(), 1.0, this);
}

QString PdfViewWindow2::getLatestContent() const { return QString(); }

void PdfViewWindow2::setMode(ViewWindowMode p_mode) {
  Q_UNUSED(p_mode);
  Q_ASSERT(false);
}

void PdfViewWindow2::handleEditorConfigChange() {
  // Always update layout mode (WidgetConfig changes don't affect editor config revision).
  ViewWindow2::handleEditorConfigChange();

  if (m_controller->checkAndUpdateConfigRevision()) {
    auto *configMgr = getServices().get<ConfigMgr2>();
    const auto &editorConfig = configMgr->getEditorConfig();
    const auto &pdfViewerConfig = editorConfig.getPdfViewerConfig();

    auto *tmplService = getServices().get<HtmlTemplateService>();
    tmplService->updatePdfViewerTemplate(pdfViewerConfig);
  }
}

void PdfViewWindow2::setModified(bool p_modified) { Q_UNUSED(p_modified); }

void PdfViewWindow2::syncEditorFromBuffer() {
  const auto &buffer = getBuffer();
  if (buffer.isValid()) {
    auto *tmplService = getServices().get<HtmlTemplateService>();
    const auto &templateHtml = tmplService->getPdfViewerTemplate();
    const auto &templatePath = tmplService->getPdfViewerTemplatePath();

    if (templateHtml.isEmpty() || templatePath.isEmpty()) {
      m_viewer->setHtml(QString());
      return;
    }

    const auto contentPath = m_controller->buildAbsolutePath(buffer.nodeId());
    auto urlState = PdfViewWindowController::preparePdfUrl(contentPath, templatePath);
    if (urlState.valid) {
      m_viewer->setHtml(templateHtml, urlState.templateUrl);
    } else {
      m_viewer->setHtml(QString());
    }
  } else {
    m_viewer->setHtml(QString());
  }
}

void PdfViewWindow2::scrollUp() {}

void PdfViewWindow2::scrollDown() {}

void PdfViewWindow2::zoom(bool p_zoomIn) { Q_UNUSED(p_zoomIn); }

PdfViewerAdapter *PdfViewWindow2::adapter() const {
  if (m_viewer) {
    return dynamic_cast<PdfViewerAdapter *>(m_viewer->adapter());
  }

  return nullptr;
}
