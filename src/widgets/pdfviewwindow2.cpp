#include "pdfviewwindow2.h"

#include <QActionGroup>
#include <QDesktopServices>
#include <QJsonArray>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QWebEnginePage>

#include <controllers/commentcontroller.h>
#include <controllers/pdfviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/pdfviewerconfig.h>
#include <core/servicelocator.h>
#include <core/services/commenttypes.h>
#include <core/services/htmltemplateservice.h>
#include <gui/services/themeservice.h>
#include <gui/services/webengineprofileservice.h>
#include <gui/utils/commentcolorswatch.h>

#include "commentprovider.h"
#include "editors/pdfviewer.h"
#include "editors/pdfvieweradapter.h"
#include "inlinebanner.h"
#include "outlinepopup.h"
#include "pdfannotationtoolbar.h"
#include "pdftooloptionsrouter.h"

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

  // AFTER setupUI(): the intents are wired onto the adapter that setupViewer()
  // created, and the first publish needs the viewer to exist.
  setupComments();
}

PdfViewWindow2::~PdfViewWindow2() {
  // The debounce timer would otherwise drop the user's last edit when the tab
  // is closed within the window.
  if (m_commentController) {
    m_commentController->flushPendingSave();
  }
  revokeDocumentToken();
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

  // A leading separator stands in for the Edit/Read toggle that opens
  // MarkdownViewWindow2's toolbar. A PDF is not editable, so there is no toggle
  // here, and without it the first action sits flush against the dock edge.
  toolBar->addSeparator();

  addLeftCommonToolBarActions(toolBar);

  // The authoring tools sit in the LEFT group, next to Save/Tag/Attachment and
  // separated from them, exactly where MarkdownViewWindow2 puts its formatting
  // actions. They are the content-authoring verbs for this window; the right
  // group is for view-level chrome (outline, layout, find, print).
  toolBar->addSeparator();
  setupAnnotationToolBarActions(toolBar);

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

// The three authoring tools, mirroring pdf.js's own toolbar layout. A MODE is
// what makes this cheap: arm Highlight once and every selection is captured,
// instead of a per-selection context-menu round trip.
//
// Each button carries its OWN settings menu (colour, plus width or font size),
// opened by the dropdown indicator; the button body still arms/disarms. The
// construction lives in PdfAnnotationToolBar so it is testable without a window
// and a WebEngine profile.
void PdfViewWindow2::setupAnnotationToolBarActions(QToolBar *p_toolBar) {
  m_annotationToolBar = new PdfAnnotationToolBar({}, QString(), this);

  auto &services = getServices();
  m_annotationToolBar->install(p_toolBar, [&services](const QString &p_iconName) {
    return ViewWindowToolBarHelper2::generateIcon(services, p_iconName);
  });

  connect(m_annotationToolBar, &PdfAnnotationToolBar::toolToggled, this,
          [this](const QString &p_tool, bool p_armed) {
            setActiveTool(p_armed ? PdfViewerAdapter::toolFromString(p_tool)
                                  : PdfViewerAdapter::Tool::None);
          });
  connect(m_annotationToolBar, &PdfAnnotationToolBar::colorPicked, this,
          &PdfViewWindow2::setToolColor);
  connect(m_annotationToolBar, &PdfAnnotationToolBar::scalarPicked, this,
          &PdfViewWindow2::setToolScalar);

  applySwatchResolvers();
}

void PdfViewWindow2::setActiveTool(PdfViewerAdapter::Tool p_tool) {
  if (auto *a = adapter()) {
    a->setTool(p_tool);
  }
  syncToolBarState();
}

QHash<QString, PdfViewerConfig::ToolOptions> PdfViewWindow2::currentToolOptions() const {
  QHash<QString, PdfViewerConfig::ToolOptions> options;
  auto *configMgr = getServices().get<ConfigMgr2>();
  if (!configMgr) {
    return options;
  }
  const auto &config = configMgr->getEditorConfig().getPdfViewerConfig();
  for (const auto &tool : PdfViewerConfig::toolNames()) {
    options.insert(tool, config.getToolOptions(tool));
  }
  return options;
}

void PdfViewWindow2::applyToolOptions(const QString &p_tool, bool p_isColor, const QString &p_token,
                                      double p_value) {
  auto *configMgr = getServices().get<ConfigMgr2>();
  if (!configMgr) {
    return;
  }
  auto &config = configMgr->getEditorConfig().getPdfViewerConfig();

  if (p_isColor) {
    PdfToolOptionsRouter::applyColor(config, adapter(), p_tool, p_token);
  } else {
    PdfToolOptionsRouter::applyScalar(config, adapter(), p_tool, p_value);
  }
  syncToolBarState();
}

void PdfViewWindow2::setToolColor(const QString &p_tool, const QString &p_token) {
  applyToolOptions(p_tool, /*p_isColor=*/true, p_token, 0.0);
}

void PdfViewWindow2::setToolScalar(const QString &p_tool, double p_value) {
  applyToolOptions(p_tool, /*p_isColor=*/false, QString(), p_value);
}

void PdfViewWindow2::hydrateToolOptions() {
  auto *configMgr = getServices().get<ConfigMgr2>();
  if (!configMgr) {
    return;
  }
  PdfToolOptionsRouter::hydrate(configMgr->getEditorConfig().getPdfViewerConfig(), adapter());
}

void PdfViewWindow2::applySwatchResolvers() {
  auto *themeService = getServices().get<ThemeService>();
  if (!themeService) {
    return;
  }

  // Captured by pointer, which is safe: ThemeService is constructed before
  // MainWindow2 (src/main.cpp), so reverse destruction tears down every widget
  // holding it first.
  CommentColorSwatch::ColorResolver resolver = [themeService](const QString &p_token) {
    return themeService->commentHighlightColor(p_token);
  };
  const auto borderCss = themeService->paletteColor(QStringLiteral("base#border"));

  if (m_annotationToolBar) {
    m_annotationToolBar->setSwatchResolver(resolver, borderCss);
  }
  if (m_viewer) {
    m_viewer->setSwatchResolver(resolver, borderCss);
  }
}

void PdfViewWindow2::setAuthoringEnabled(bool p_enabled) {
  if (m_annotationToolBar) {
    m_annotationToolBar->setAuthoringEnabled(p_enabled);
  }

  // Disarm whatever was active, or the page would stay in a tool the toolbar
  // can no longer show or cancel.
  if (!p_enabled) {
    setActiveTool(PdfViewerAdapter::Tool::None);
  }
}

void PdfViewWindow2::syncToolBarState() {
  auto *a = adapter();
  if (!a || !m_annotationToolBar) {
    return;
  }

  m_annotationToolBar->syncState(PdfViewerAdapter::toolToString(a->getTool()),
                                 currentToolOptions());
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

QSharedPointer<CommentProvider> PdfViewWindow2::getCommentProvider() const {
  return m_commentProvider;
}

void PdfViewWindow2::setupComments() {
  m_commentProvider.reset(new CommentProvider(nullptr));
  m_commentController = new CommentController(getServices(), this);

  auto *pdfAdapter = adapter();

  // === Controller -> provider (the dock) and the overlay ===
  connect(m_commentController, &CommentController::commentsChanged, this,
          [this](const CommentSet &p_comments) {
            m_commentProvider->setComments(p_comments);
            publishCommentsToViewer();
          });

  connect(m_commentController, &CommentController::selectionChanged, this,
          [this, pdfAdapter](const QString &p_id) {
            m_commentProvider->setSelectedId(p_id);
            if (pdfAdapter && !p_id.isEmpty()) {
              pdfAdapter->scrollToComment(p_id);
            }
          });

  connect(m_commentController, &CommentController::editableChanged, this, [this](bool p_editable) {
    m_commentProvider->setEditable(p_editable);
    // Authoring must be impossible, not merely rejected: a stroke drawn
    // on a read-only file would be dispatched, refused, and leave the
    // user looking at something that was never saved.
    setAuthoringEnabled(p_editable);
  });

  connect(m_commentController, &CommentController::failed, this, [this](const QString &p_message) {
    // Surfaced without touching the buffer's modified state: comments are not
    // buffer content, and the PDF itself genuinely never changes.
    qWarning() << "PdfViewWindow2: comment store error:" << p_message;
    if (!m_commentBanner) {
      m_commentBanner = new InlineBanner(InlineBanner::Severity::Error, p_message, this);
      addTopWidget(m_commentBanner);
    } else {
      m_commentBanner->setText(p_message);
      m_commentBanner->show();
    }
  });

  // === Overlay (view) -> controller ===
  if (pdfAdapter) {
    connect(pdfAdapter, &PdfViewerAdapter::addCommentRequested, m_commentController,
            &CommentController::addComment);
    connect(pdfAdapter, &PdfViewerAdapter::selectCommentRequested, m_commentController,
            &CommentController::selectComment);
    connect(pdfAdapter, &PdfViewerAdapter::deleteCommentRequested, m_commentController,
            &CommentController::deleteComment);
  }

  // === Page context menu (view) -> overlay ===
  // The discoverable way to create a highlight. It has to round-trip through
  // the adapter because only the web side knows what is selected and where.
  //
  // It goes through the SAME router the toolbar menu uses, so the pick is also
  // persisted as the highlight tool's colour and the two pickers cannot
  // disagree.
  connect(m_viewer, &PdfViewer::highlightSelectionRequested, this, [this](const QString &p_color) {
    auto *configMgr = getServices().get<ConfigMgr2>();
    if (!configMgr) {
      return;
    }
    PdfToolOptionsRouter::captureHighlight(configMgr->getEditorConfig().getPdfViewerConfig(),
                                           adapter(), p_color);
    syncToolBarState();
  });

  // === Dock (view) -> controller ===
  connect(m_commentProvider.data(), &CommentProvider::activateRequested, m_commentController,
          &CommentController::selectComment);
  connect(m_commentProvider.data(), &CommentProvider::textEditRequested, m_commentController,
          &CommentController::setCommentText);
  connect(m_commentProvider.data(), &CommentProvider::colorChangeRequested, m_commentController,
          &CommentController::setCommentColor);
  connect(m_commentProvider.data(), &CommentProvider::deleteRequested, m_commentController,
          &CommentController::deleteComment);

  const auto &buffer = getBuffer();
  m_commentController->setActiveFile(buffer.isValid() ? buffer.nodeId() : NodeIdentifier());

  // The web side can leave a tool by itself (Esc, or the one-shot Text tool
  // completing), so the toolbar follows the adapter rather than assuming its
  // own toggles are authoritative.
  if (pdfAdapter) {
    connect(pdfAdapter, &PdfViewerAdapter::toolFinished, this, &PdfViewWindow2::syncToolBarState);

    // BEFORE the first ready transition, so the reload latch republishes the
    // persisted settings rather than the adapter's own defaults.
    hydrateToolOptions();
  }
  syncToolBarState();
}

void PdfViewWindow2::handleNodeRetargeted(const NodeIdentifier &p_newNodeId) {
  // Without this the controller keeps saving to the pre-rename path: the sidecar
  // visible under the new name would silently stop receiving edits.
  if (m_commentController) {
    m_commentController->retargetActiveFile(p_newNodeId);
  }
}

void PdfViewWindow2::publishCommentsToViewer() {
  auto *pdfAdapter = adapter();
  if (!pdfAdapter || !m_commentController) {
    return;
  }

  QJsonArray payload;
  for (const auto &comment : m_commentController->getComments().m_comments) {
    payload.append(comment.toJson());
  }
  pdfAdapter->setComments(payload);
}

void PdfViewWindow2::setupViewer() {
  Q_ASSERT(!m_viewer);

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &pdfViewerConfig = editorConfig.getPdfViewerConfig();

  m_controller->checkAndUpdateConfigRevision();

  auto *themeService = getServices().get<ThemeService>();

  // Prepare the PDF.js HTML template via HtmlTemplateService (DI, not legacy singleton).
  // The comment colors are RESOLVED here, in the view layer, and passed down as
  // plain content — HtmlTemplateService must stay free of GUI dependencies.
  auto *tmplService = getServices().get<HtmlTemplateService>();
  tmplService->updatePdfViewerTemplate(pdfViewerConfig,
                                       themeService->commentHighlightCssVariables());

  auto *pdfAdapter = new PdfViewerAdapter(nullptr);
  auto *profileService = getServices().get<WebEngineProfileService>();
  m_viewer = new PdfViewer(
      pdfAdapter, themeService->getBaseBackground(), 1.0, this,
      profileService ? profileService->profile() : nullptr,
      [themeService](const QString &p_token) {
        return themeService->commentHighlightColor(p_token);
      },
      themeService->paletteColor(QStringLiteral("base#border")));
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

    auto *themeService = getServices().get<ThemeService>();
    auto *tmplService = getServices().get<HtmlTemplateService>();
    tmplService->updatePdfViewerTemplate(pdfViewerConfig,
                                         themeService->commentHighlightCssVariables());
  }
}

void PdfViewWindow2::handleThemeChanged() {
  ViewWindow2::handleThemeChanged();

  if (!m_viewer) {
    return;
  }

  // Force-regenerate PDF template (theme may have changed background colors in CSS,
  // and the comment highlight custom properties are theme-resolved).
  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &pdfViewerConfig = configMgr->getEditorConfig().getPdfViewerConfig();
  auto *themeService = getServices().get<ThemeService>();
  auto *tmplService = getServices().get<HtmlTemplateService>();
  tmplService->updatePdfViewerTemplate(pdfViewerConfig,
                                       themeService->commentHighlightCssVariables(),
                                       /*p_force=*/true);

  // Update WebEngine page background color.
  m_viewer->page()->setBackgroundColor(themeService->getBaseBackground());

  // The colour chips are theme-dependent, and the BORDER travels as a value
  // rather than a callback, so both must be re-supplied — not merely redrawn.
  applySwatchResolvers();

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
  //
  // setReady(false) is the page-generation handshake: the replacement page's
  // setReady(true) would otherwise be a no-op and nothing queued would ever be
  // republished onto the new QWebChannel.
  if (auto *pdfAdapter = adapter()) {
    pdfAdapter->clearOutline();
    pdfAdapter->clearComments();
    pdfAdapter->setReady(false);
  }

  // Replace-then-revoke: the new token is registered before the old one is
  // dropped, so the two can never collide and a failed registration leaves the
  // window blank rather than pointing at a stale document.
  const auto previousToken = m_documentToken;
  m_documentToken.clear();

  auto *profileService = getServices().get<WebEngineProfileService>();

  const auto &buffer = getBuffer();
  if (buffer.isValid() && profileService) {
    auto *tmplService = getServices().get<HtmlTemplateService>();
    const auto &templateHtml = tmplService->getPdfViewerTemplate();

    if (!templateHtml.isEmpty()) {
      const auto contentPath = m_controller->buildAbsolutePath(buffer.nodeId());
      m_documentToken = profileService->registerPdfDocument(contentPath);
      auto urlState = PdfViewWindowController::preparePdfUrl(m_documentToken);
      if (urlState.valid) {
        m_viewer->load(urlState.templateUrl);
      } else {
        m_viewer->setHtml(QString());
      }
    } else {
      m_viewer->setHtml(QString());
    }
  } else {
    m_viewer->setHtml(QString());
  }

  if (!previousToken.isEmpty() && profileService) {
    profileService->unregisterPdfDocument(previousToken);
  }

  // Re-latch the current set onto the (not-yet-ready) replacement page. The
  // adapter publishes it exactly once when the new QWebChannel comes up.
  publishCommentsToViewer();
}

void PdfViewWindow2::revokeDocumentToken() {
  if (m_documentToken.isEmpty()) {
    return;
  }
  if (auto *profileService = getServices().get<WebEngineProfileService>()) {
    profileService->unregisterPdfDocument(m_documentToken);
  }
  m_documentToken.clear();
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
