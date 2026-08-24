#include "pdfviewwindow2.h"

#include <QDesktopServices>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QWebEnginePage>

#include <controllers/pdfviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/pdfviewerconfig.h>
#include <core/servicelocator.h>
#include <core/services/htmltemplateservice.h>
#include <gui/services/themeservice.h>
#include <gui/services/webengineprofileservice.h>

#include "editors/pdfviewer.h"
#include "editors/pdfvieweradapter.h"
#include "outlinepopup.h"

using namespace vnotex;

namespace {
// The PDF twin of MarkdownViewWindow2::headingsToOutline(): publish only name +
// level upward; the destination index stays private to PdfViewerAdapter.
QSharedPointer<Outline> outlineFromHeadings(const QVector<PdfViewerAdapter::Heading> &p_headings) {
  auto outline = QSharedPointer<Outline>::create();
  outline->m_headings.reserve(p_headings.size());
  for (const auto &heading : p_headings) {
    outline->m_headings.push_back(Outline::Heading(heading.m_name, heading.m_level));
  }

  return outline;
}
} // namespace

PdfViewWindow2::PdfViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                               QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  m_controller = new PdfViewWindowController(p_services, this);
  m_mode = ViewWindowMode::Read;

  // MUST run before setupUI(): setupToolBar() hands the provider to the Outline
  // popup. The headingClicked lambda resolves adapter() lazily, so the adapter
  // does not need to exist yet.
  setupOutlineProvider();

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

  addLeftCommonToolBarActions(toolBar);
  addRightCommonToolBarActions(toolBar);
}

void PdfViewWindow2::addAdditionalRightToolBarActions(QToolBar *p_toolBar) {
  // Outline popup button (right corner, first): wire it to this window's outline provider.
  auto *outlineAct = addAction(p_toolBar, ViewWindowToolBarHelper2::Outline);
  auto *toolBtn = dynamic_cast<QToolButton *>(p_toolBar->widgetForAction(outlineAct));
  if (toolBtn) {
    auto *outlinePopup = dynamic_cast<OutlinePopup *>(toolBtn->menu());
    if (outlinePopup) {
      outlinePopup->setOutlineProvider(m_outlineProvider);
    }
  }
}

void PdfViewWindow2::setupOutlineProvider() {
  m_outlineProvider.reset(new OutlineProvider(nullptr));

  connect(m_outlineProvider.data(), &OutlineProvider::headingClicked, this, [this](int p_idx) {
    if (adapter()) {
      adapter()->scrollToOutlineItem(p_idx);
    }
  });
}

QSharedPointer<OutlineProvider> PdfViewWindow2::getOutlineProvider() const {
  return m_outlineProvider;
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
  auto *profileService = getServices().get<WebEngineProfileService>();
  m_viewer = new PdfViewer(pdfAdapter, themeService->getBaseBackground(), 1.0, this,
                           profileService ? profileService->profile() : nullptr);
  connect(m_viewer, &WebViewer::localFileOpenRequested, this, [](const QUrl &p_url) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(p_url.toLocalFile()));
  });

  // Outline pipeline: PDF bookmarks -> OutlineProvider.
  connect(pdfAdapter, &PdfViewerAdapter::outlineChanged, this, [this]() {
    m_outlineProvider->setOutline(outlineFromHeadings(adapter()->getOutlineHeadings()));
  });
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

void PdfViewWindow2::handleThemeChanged() {
  ViewWindow2::handleThemeChanged();

  if (!m_viewer) {
    return;
  }

  // Force-regenerate PDF template (theme may have changed background colors in CSS).
  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &pdfViewerConfig = configMgr->getEditorConfig().getPdfViewerConfig();
  auto *tmplService = getServices().get<HtmlTemplateService>();
  tmplService->updatePdfViewerTemplate(pdfViewerConfig, /*p_force=*/true);

  // Update WebEngine page background color.
  auto *themeService = getServices().get<ThemeService>();
  m_viewer->page()->setBackgroundColor(themeService->getBaseBackground());

  // Reload the viewer content with the new template.
  syncEditorFromBuffer();
}

void PdfViewWindow2::setModified(bool p_modified) { Q_UNUSED(p_modified); }

void PdfViewWindow2::syncEditorFromBuffer() {
  // Every path below reloads (or blanks) the page. The adapter outlives the web
  // page and WebViewAdapter::setReady() early-returns when already ready, so
  // without this the previous document's headings would linger until the new
  // page happens to publish its own. clearOutline() -> outlineChanged -> the
  // provider is emptied for free.
  if (auto *pdfAdapter = adapter()) {
    pdfAdapter->clearOutline();
  }

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
