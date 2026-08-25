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

#include "commentprovider.h"
#include "editors/pdfviewer.h"
#include "editors/pdfvieweradapter.h"
#include "inlinebanner.h"
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

  addLeftCommonToolBarActions(toolBar);
  addRightCommonToolBarActions(toolBar);
}

void PdfViewWindow2::addAdditionalRightToolBarActions(QToolBar *p_toolBar) {
  setupAnnotationToolBarActions(p_toolBar);

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
void PdfViewWindow2::setupAnnotationToolBarActions(QToolBar *p_toolBar) {
  m_toolGroup = new QActionGroup(this);
  // Non-exclusive: clicking the armed tool again must DISARM it, which an
  // exclusive group forbids (it keeps one member checked forever).
  m_toolGroup->setExclusive(false);

  const auto addTool = [this, p_toolBar](PdfViewerAdapter::Tool p_tool, const QString &p_icon,
                                         const QString &p_text) {
    auto *act =
        p_toolBar->addAction(ViewWindowToolBarHelper2::generateIcon(getServices(), p_icon), p_text);
    act->setCheckable(true);
    act->setData(static_cast<int>(p_tool));
    m_toolGroup->addAction(act);
    connect(act, &QAction::triggered, this, [this, p_tool](bool p_checked) {
      setActiveTool(p_checked ? p_tool : PdfViewerAdapter::Tool::None);
    });
    return act;
  };

  addTool(PdfViewerAdapter::Tool::Highlight, QStringLiteral("type_mark_editor.svg"),
          tr("Highlight"));
  addTool(PdfViewerAdapter::Tool::Ink, QStringLiteral("edit_editor.svg"), tr("Draw"));
  addTool(PdfViewerAdapter::Tool::FreeText, QStringLiteral("type_code_editor.svg"), tr("Text box"));

  // Colour applies to all three tools, so it sits beside them rather than
  // inside any one of them.
  m_colorButton = new QToolButton(p_toolBar);
  m_colorButton->setPopupMode(QToolButton::InstantPopup);
  m_colorButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_colorButton->setAutoRaise(true);
  m_colorButton->setProperty("NoMenuIndicator", true);

  auto *colorMenu = new QMenu(m_colorButton);
  for (const auto &token : CommentColor::all()) {
    auto *act = colorMenu->addAction(CommentColor::displayName(token));
    act->setCheckable(true);
    act->setData(token);
    connect(act, &QAction::triggered, this, [this, token]() { setActiveColor(token); });
  }
  m_colorButton->setMenu(colorMenu);
  p_toolBar->addWidget(m_colorButton);
}

void PdfViewWindow2::setActiveTool(PdfViewerAdapter::Tool p_tool) {
  if (auto *a = adapter()) {
    a->setTool(p_tool);
  }
  syncToolBarState();
}

void PdfViewWindow2::setActiveColor(const QString &p_color) {
  if (auto *a = adapter()) {
    a->setCommentColor(p_color);
  }
  // Persisted, so the next window and the next launch start where the user left
  // off rather than back at yellow.
  if (auto *configMgr = getServices().get<ConfigMgr2>()) {
    configMgr->getEditorConfig().getPdfViewerConfig().setCommentColor(p_color);
  }
  syncToolBarState();
}

void PdfViewWindow2::setAuthoringEnabled(bool p_enabled) {
  if (m_toolGroup) {
    for (auto *act : m_toolGroup->actions()) {
      act->setEnabled(p_enabled);
    }
  }
  if (m_colorButton) {
    m_colorButton->setEnabled(p_enabled);
  }

  // Disarm whatever was active, or the page would stay in a tool the toolbar
  // can no longer show or cancel.
  if (!p_enabled) {
    setActiveTool(PdfViewerAdapter::Tool::None);
  }
}

void PdfViewWindow2::syncToolBarState() {
  auto *a = adapter();
  if (!a) {
    return;
  }

  if (m_toolGroup) {
    for (auto *act : m_toolGroup->actions()) {
      const auto tool = static_cast<PdfViewerAdapter::Tool>(act->data().toInt());
      QSignalBlocker blocker(act);
      act->setChecked(tool == a->getTool());
    }
  }

  if (m_colorButton) {
    const auto color = a->getCommentColor();
    m_colorButton->setText(CommentColor::displayName(color));
    if (auto *menu = m_colorButton->menu()) {
      for (auto *act : menu->actions()) {
        QSignalBlocker blocker(act);
        act->setChecked(act->data().toString() == color);
      }
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
  connect(m_viewer, &PdfViewer::highlightSelectionRequested, this, [this](const QString &p_color) {
    if (auto *a = adapter()) {
      a->captureSelection(p_color);
    }
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

    if (auto *configMgr = getServices().get<ConfigMgr2>()) {
      pdfAdapter->setCommentColor(
          configMgr->getEditorConfig().getPdfViewerConfig().getCommentColor());
    }
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
