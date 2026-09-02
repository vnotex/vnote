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
#include "pdfviewertoolbar.h"

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
  // group is for view-level chrome (outline, layout, find).
  toolBar->addSeparator();
  setupAnnotationToolBarActions(toolBar);

  addRightCommonToolBarActions(toolBar);

  // LAST, deliberately: the overflow "More" button is the toolbar's catch-all
  // and belongs at the very end, after Readable Width, Presentation Mode and
  // Find And Replace. Those are appended by addRightCommonToolBarActions()
  // above, so this is the only place that can reach the position.
  if (m_viewerToolBar) {
    auto &services = getServices();
    m_viewerToolBar->installOverflowAction(toolBar, [&services](const QString &p_iconName) {
      return ViewWindowToolBarHelper2::generateIcon(services, p_iconName);
    });
  }
}

void PdfViewWindow2::addAdditionalRightToolBarActions(QToolBar *p_toolBar) {
  // The native replacements for pdf.js's hidden built-in strip. The Outline
  // popup is inserted by the hook below, between the sidebar toggle and the
  // page controls -- both are view chrome of the same kind, and the sequence
  // mirrors where pdf.js put them.
  setupViewerToolBarActions(p_toolBar);
  // ViewWindow2::addRightCommonToolBarActions() appends ToggleLayoutMode, then
  // calls addAdditionalViewToolBarActions() (where Presentation Mode lands),
  // then Find And Replace. Print is opted out of; see isPrintSupported().
}

// Presentation Mode sits with Readable Width rather than in the overflow menu:
// both change how the content is PRESENTED, and that is where a reader looks
// for them. The intent is already connected in setupViewerToolBarActions(),
// which runs earlier in the same toolbar build.
void PdfViewWindow2::addAdditionalViewToolBarActions(QToolBar *p_toolBar) {
  if (!m_viewerToolBar) {
    return;
  }
  auto &services = getServices();
  m_viewerToolBar->installPresentationAction(p_toolBar, [&services](const QString &p_iconName) {
    return ViewWindowToolBarHelper2::generateIcon(services, p_iconName);
  });
}

// The sidebar / outline / page / zoom / overflow controls that stand in for
// pdf.js's own toolbar (hidden by pdfviewer.css). The construction lives in
// PdfViewerToolBar so it is testable with a bare QToolBar and no WebEngine
// profile; this function is only the wiring.
//
// The adapter is resolved LAZILY inside every lambda, matching the headingClicked
// comment above: setupToolBar() runs after setupViewer() today, but nothing in
// the toolbar's contract depends on that ordering.
void PdfViewWindow2::setupViewerToolBarActions(QToolBar *p_toolBar) {
  m_viewerToolBar = new PdfViewerToolBar(this);

  auto &services = getServices();
  m_viewerToolBar->install(
      p_toolBar,
      [&services](const QString &p_iconName) {
        return ViewWindowToolBarHelper2::generateIcon(services, p_iconName);
      },
      [this, p_toolBar]() {
        // Outline popup button: wire it to this window's outline provider. It
        // needs the ServiceLocator and the provider, which is exactly why
        // PdfViewerToolBar takes a hook rather than building it itself.
        auto *outlineAct = addAction(p_toolBar, ViewWindowToolBarHelper2::Outline);
        auto *toolBtn = dynamic_cast<QToolButton *>(p_toolBar->widgetForAction(outlineAct));
        if (toolBtn) {
          auto *outlinePopup = dynamic_cast<OutlinePopup *>(toolBtn->menu());
          if (outlinePopup) {
            outlinePopup->setOutlineProvider(m_outlineProvider);
          }
        }
      });

  connect(m_viewerToolBar, &PdfViewerToolBar::pageRequested, this, [this](int p_page) {
    if (auto *a = adapter()) {
      a->gotoPage(p_page);
    }
  });
  connect(m_viewerToolBar, &PdfViewerToolBar::zoomRequested, this, [this](const QString &p_value) {
    if (auto *a = adapter()) {
      a->setZoom(p_value);
    }
  });
  connect(m_viewerToolBar, &PdfViewerToolBar::zoomStepRequested, this, [this](bool p_zoomIn) {
    if (auto *a = adapter()) {
      a->stepZoom(p_zoomIn);
    }
  });
  connect(m_viewerToolBar, &PdfViewerToolBar::rotationRequested, this, [this](int p_degrees) {
    if (auto *a = adapter()) {
      a->setRotation(p_degrees);
    }
  });
  connect(m_viewerToolBar, &PdfViewerToolBar::scrollModeRequested, this, [this](int p_mode) {
    if (auto *a = adapter()) {
      a->setScrollMode(p_mode);
    }
  });
  connect(m_viewerToolBar, &PdfViewerToolBar::spreadModeRequested, this, [this](int p_mode) {
    if (auto *a = adapter()) {
      a->setSpreadMode(p_mode);
    }
  });
  connect(m_viewerToolBar, &PdfViewerToolBar::cursorToolRequested, this, [this](int p_tool) {
    if (auto *a = adapter()) {
      a->setCursorTool(p_tool);
    }
  });
  connect(m_viewerToolBar, &PdfViewerToolBar::sidebarToggleRequested, this, [this]() {
    if (auto *a = adapter()) {
      a->toggleSidebar();
    }
    // The tick is NOT set from the click: pdf.js answers with
    // 'sidebarviewchanged', and the toolbar repaints from that. Repaint now so a
    // refused toggle snaps back rather than showing a state that never happened.
    syncViewerToolBarState();
  });
  connect(m_viewerToolBar, &PdfViewerToolBar::presentationModeRequested, this,
          &PdfViewWindow2::togglePresentationMode);
  connect(m_viewerToolBar, &PdfViewerToolBar::documentPropertiesRequested, this, [this]() {
    if (auto *a = adapter()) {
      a->showDocumentProperties();
    }
  });

  if (auto *a = adapter()) {
    connect(a, &PdfViewerAdapter::viewerStateChanged, this,
            &PdfViewWindow2::syncViewerToolBarState);
    // Without this the find bar shows no match count at all -- the analogue of
    // MarkdownViewWindow2's findTextReady connection.
    connect(a, &PdfViewerAdapter::findTextReady, this,
            [this](const QStringList &p_texts, int p_totalMatches, int p_currentMatchIndex) {
              showFindResult(p_texts, p_totalMatches, p_currentMatchIndex);
            });
  }

  syncViewerToolBarState();
}

void PdfViewWindow2::syncViewerToolBarState() {
  if (!m_viewerToolBar) {
    return;
  }
  auto *a = adapter();
  m_viewerToolBar->syncState(a ? a->getViewerState() : PdfViewerAdapter::ViewerState());
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
  connect(m_annotationToolBar, &PdfAnnotationToolBar::opacityPicked, this,
          &PdfViewWindow2::setToolOpacity);

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

void PdfViewWindow2::setToolOpacity(const QString &p_tool, double p_value) {
  auto *configMgr = getServices().get<ConfigMgr2>();
  if (!configMgr) {
    return;
  }
  auto &config = configMgr->getEditorConfig().getPdfViewerConfig();
  PdfToolOptionsRouter::applyOpacity(config, adapter(), p_tool, p_value);
  syncToolBarState();
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
  const auto borderCss = themeService->paletteColor(QStringLiteral("base#normal#border"));

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
    // The overlay needs it too, or a double-click would open an inline editor
    // whose every keystroke the store is going to throw away.
    if (auto *a = adapter()) {
      a->setCommentsEditable(p_editable);
    }
  });

  // The Text tool is a PLACE-then-TYPE gesture, and the typing half lives on
  // the page. Connected after commentsChanged above, so the overlay already
  // holds the new comment by the time the editor is opened on it.
  connect(m_commentController, &CommentController::commentAdded, this,
          &PdfViewWindow2::beginInlineTextEdit);

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
    connect(pdfAdapter, &PdfViewerAdapter::setCommentTextRequested, m_commentController,
            &CommentController::setCommentText);
    connect(pdfAdapter, &PdfViewerAdapter::moveCommentRequested, m_commentController,
            &CommentController::moveComment);
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

void PdfViewWindow2::beginInlineTextEdit(const QString &p_id) {
  auto *pdfAdapter = adapter();
  if (!pdfAdapter || !m_commentController) {
    return;
  }

  const auto &comments = m_commentController->getComments();
  const int idx = comments.indexOfId(p_id);
  if (idx < 0) {
    return;
  }

  const auto &comment = comments.m_comments[idx];
  if (comment.m_anchor.value(QStringLiteral("type")).toString() != PdfFreeTextAnchor::type()) {
    // A highlight or a stroke has no on-page body; its note is written in the
    // dock.
    return;
  }

  pdfAdapter->beginCommentTextEdit(p_id);
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
      themeService->paletteColor(QStringLiteral("base#normal#border")));
  connect(m_viewer, &WebViewer::localFileOpenRequested, this, [](const QUrl &p_url) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(p_url.toLocalFile()));
  });

  // Names the mode the user actually entered. "Exit Full Screen" would read as
  // the application's own full-screen toggle and send them to the View menu.
  setContentFullScreenExitText(tr("Exit Presentation Mode"));

  // Escape leaves presentation mode. Driven entirely from Qt: see
  // togglePresentationMode() for why pdf.js's own HTML5-fullscreen presentation
  // mode is not used.
  connect(this, &ViewWindow2::contentFullScreenExitRequested, this,
          [this]() { setPresentationMode(false); });

  // Outline pipeline: PDF bookmarks -> OutlineProvider.
  connect(pdfAdapter, &PdfViewerAdapter::outlineChanged, this, [this]() {
    m_outlineProvider->setOutline(outlineFromHeadings(adapter()->getOutlineHeadings()));
  });
}

// ============ Printing is deliberately not offered ============
//
// A pdf.js viewer cannot be printed reliably from Qt, and a Print button that
// produces a blank or partial document is worse than no Print button. The
// user's PDF is a file on disk; the OS reader prints it correctly.
//
// The two obvious routes are both broken:
//
//   * QWebEngineView::print() alone prints the LIVE DOM, which holds only the
//     pages Chromium has scrolled into view. pdf.js's print service -- the
//     thing that renders every page into #printContainer -- is unreachable
//     that way: pdf.js installs a `beforeprint` listener that calls
//     stopImmediatePropagation() on any event whose detail is not "custom"
//     (web/viewer.mjs:14134), and the native beforeprint Qt fires is exactly
//     that.
//
//   * Driving pdf.js's own window.print() first and printing from
//     QWebEngineView::printRequested does reach the print service, but races
//     it: performPrint() calls abort() on a hardcoded 20 ms timer
//     (web/viewer.mjs:14052), which empties #printContainer and revokes every
//     page's blob URL, while QWebEngineView::print() is asynchronous and gives
//     no signal that Chromium has captured the DOM. Nothing on the Qt side can
//     win that race deterministically.
//
// Closing the gap needs either a patch to the vendored pdf.js (which every
// upgrade would have to re-apply) or a VNote-owned print pipeline -- most
// plausibly rendering the file with QPdfDocument and painting it onto a
// QPrinter, with no pdf.js involvement at all. Until one of those exists, the
// action is omitted rather than shipped dead.
bool PdfViewWindow2::isPrintSupported() const { return false; }

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

  // ViewWindow2::handleThemeChanged() -> refreshToolBarIcons() covers the plain
  // toolbar actions, but it iterates the TOOLBAR's actions only: the overflow
  // menu's entries and the overflow QToolButton itself are unreachable that way.
  if (m_viewerToolBar) {
    auto &services = getServices();
    m_viewerToolBar->refreshIcons([&services](const QString &p_iconName) {
      return ViewWindowToolBarHelper2::generateIcon(services, p_iconName);
    });
  }

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
    // Drops the DOCUMENT-dependent viewer state (page count, validity) and ARMS
    // the page/zoom/rotation/mode replay. The replay VALUES deliberately
    // survive: this runs on every reload, and a theme switch is a reload -- so
    // without it a theme change would silently jump back to page 1.
    pdfAdapter->clearViewerState();
    // The find highlight belongs to the page being torn down.
    pdfAdapter->clearFind();
    pdfAdapter->setReady(false);
  }
  // Re-disables every viewer control until the replacement document reports a
  // state.
  syncViewerToolBarState();

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

void PdfViewWindow2::scrollUp() {
  auto *a = adapter();
  if (!a) {
    return;
  }
  const auto &state = a->getViewerState();
  if (state.m_valid && state.m_page > 1) {
    a->gotoPage(state.m_page - 1);
  }
}

void PdfViewWindow2::scrollDown() {
  auto *a = adapter();
  if (!a) {
    return;
  }
  const auto &state = a->getViewerState();
  if (state.m_valid && state.m_page < state.m_pageCount) {
    a->gotoPage(state.m_page + 1);
  }
}

void PdfViewWindow2::zoom(bool p_zoomIn) {
  if (auto *a = adapter()) {
    a->stepZoom(p_zoomIn);
  }
}

// ============ Presentation mode ============
//
// Driven entirely from Qt, and deliberately NOT through pdf.js's own
// presentation mode.
//
// pdf.js builds that on HTML5 fullscreen, and Chromium requires transient
// renderer USER ACTIVATION for requestFullscreen(). A click on a Qt QAction
// relayed over QWebChannel carries none, so the request is refused -- and
// pdf.js swallows the rejected promise, which makes the whole feature a
// silently dead button. (pdf.js does not even CONSTRUCT its presentation-mode
// object unless `document.fullscreenEnabled` is true, which additionally
// requires QWebEngineSettings::FullScreenSupportEnabled; see the note in
// webviewer.h for why that is not turned on globally.)
//
// So VNote does the two halves itself: ViewWindow2 lifts the viewer into a
// fullscreen top-level, and the adapter puts the document into page-fit +
// page-at-a-time scrolling. Both halves are things we already own and can test.

void PdfViewWindow2::togglePresentationMode() { setPresentationMode(!isContentFullScreen()); }

void PdfViewWindow2::setPresentationMode(bool p_on) {
  if (p_on == isContentFullScreen()) {
    return;
  }

  auto *a = adapter();

  if (p_on) {
    if (!a || !a->getViewerState().m_valid) {
      // No document: a fullscreen blank page is worse than an inert button.
      return;
    }
    // Captured BEFORE the switch, so leaving restores what the user was
    // actually looking at rather than a hardcoded default.
    m_prePresentationZoom = a->getViewerState().m_scaleValue;
    m_prePresentationScrollMode = a->getViewerState().m_scrollMode;

    if (!setContentFullScreen(true)) {
      return;
    }
    a->setZoom(QStringLiteral("page-fit"));
    // pdf.js ScrollMode::PAGE.
    a->setScrollMode(3);
    return;
  }

  setContentFullScreen(false);
  if (a) {
    if (!m_prePresentationZoom.isEmpty()) {
      a->setZoom(m_prePresentationZoom);
    }
    a->setScrollMode(m_prePresentationScrollMode);
  }
  m_prePresentationZoom.clear();
}

// ============ Find and Replace ============
//
// VNote's find bar drives pdf.js's findController rather than
// QWebEngineView::findText, because pdf.js builds a text layer only for VISIBLE
// pages -- a native find would silently miss every page never scrolled into
// view.

void PdfViewWindow2::handleFindTextChanged(const QString &p_text, FindOptions p_options) {
  if (!(p_options & FindOption::IncrementalSearch)) {
    return;
  }
  if (auto *a = adapter()) {
    a->findText(QStringList(p_text), p_options);
  }
}

void PdfViewWindow2::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  if (auto *a = adapter()) {
    a->findText(p_texts, p_options);
  }
}

void PdfViewWindow2::handleFindAndReplaceWidgetOpened() {
  // A PDF is never written, so Replace has nothing to do -- and the base class's
  // handleReplace/handleReplaceAll are no-ops, so leaving the buttons live would
  // make them silently do nothing. Regular expressions are unsupported by
  // pdf.js's findController and are refused on the JS side; disabling the box
  // says so before the user tries.
  setFindAndReplaceReplaceEnabled(false);
  setFindAndReplaceOptionsEnabled(FindOption::RegularExpression, false);
}

void PdfViewWindow2::handleFindAndReplaceWidgetClosed() {
  if (auto *a = adapter()) {
    a->clearFind();
  }
}

PdfViewerAdapter *PdfViewWindow2::adapter() const {
  if (m_viewer) {
    return dynamic_cast<PdfViewerAdapter *>(m_viewer->adapter());
  }

  return nullptr;
}
