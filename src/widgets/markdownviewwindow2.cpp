#include "markdownviewwindow2.h"

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QPrinter>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>

#include <vtextedit/pegmarkdownhighlighter.h>
#include <vtextedit/vtextedit.h>

#include <controllers/markdowneditorcontroller.h>
#include <controllers/markdownviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/exception.h>
#include <core/markdowneditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/htmltemplateservice.h>
#include <core/theme.h>
#include <gui/services/themeservice.h>
#include <gui/utils/printutils.h>
#include <gui/utils/widgetutils.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>

#include "editors/markdowneditor.h"
#include "editors/markdownviewer.h"
#include "editors/markdownvieweradapter.h"
#include "editors/previewhelper.h"
#include "editors/statuswidget.h"
#include "textviewwindowhelper.h"
#include "viewwindowtoolbarhelper2.h"

using namespace vnotex;

// ============ Constructor ============

MarkdownViewWindow2::MarkdownViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                         QWidget *p_parent,
                                         ViewWindowMode p_initialMode)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  // Start at Invalid so the first setMode() triggers full initialization.
  m_mode = ViewWindowMode::Invalid;
  m_editorController = new MarkdownEditorController(p_services, this);
  m_windowController = new MarkdownViewWindowController(p_services, this);

  setupUI();
  setupPreviewHelper();

  // Trigger initial mode setup directly to the requested mode.
  // Going straight to Edit (Invalid -> Edit) avoids a visible Read -> Edit
  // transition (flash of blank viewer in QSplitter).
  setModeInternal(p_initialMode, true);
}

// ============ Destructor ============

MarkdownViewWindow2::~MarkdownViewWindow2() {
  // Remove status widgets from QStackedWidget and reparent to nullptr
  // to prevent double-free with QSharedPointer.
  if (m_textEditorStatusWidget) {
    m_mainStatusWidget->removeWidget(m_textEditorStatusWidget.get());
    m_textEditorStatusWidget->setParent(nullptr);
  }
  if (m_viewerStatusWidget) {
    m_mainStatusWidget->removeWidget(m_viewerStatusWidget.get());
    m_viewerStatusWidget->setParent(nullptr);
  }
  m_mainStatusWidget->setParent(nullptr);
}

// ============ setupUI ============

void MarkdownViewWindow2::setupUI() {
  // Central widget: splitter to hold editor (index 0) and viewer (index 1).
  m_splitter = new QSplitter(this);
  m_splitter->setContentsMargins(0, 0, 0, 0);
  setCentralWidget(m_splitter);
  // Get the focus event from splitter.
  m_splitter->installEventFilter(this);

  // Status widget: QStackedWidget to switch between editor/viewer status.
  {
    auto statusWidget = QSharedPointer<StatusWidget>::create();
    m_mainStatusWidget = QSharedPointer<QStackedWidget>(new QStackedWidget());
    m_mainStatusWidget->setContentsMargins(0, 0, 0, 0);
    statusWidget->setEditorStatusWidget(m_mainStatusWidget.staticCast<QWidget>());
    setStatusWidget(statusWidget);
  }

  setupToolBar();
}

// ============ setupToolBar ============

void MarkdownViewWindow2::setupToolBar() {
  auto *toolBar = createToolBar(this);
  addToolBar(toolBar);

  addAction(toolBar, ViewWindowToolBarHelper2::EditRead);
  addAction(toolBar, ViewWindowToolBarHelper2::Save);
  addAction(toolBar, ViewWindowToolBarHelper2::WordCount);

  // Separator between word count and formatting actions (visible only in edit mode).
  auto *typeSeparator = toolBar->addSeparator();
  typeSeparator->setVisible(false);
  connect(this, &ViewWindow2::modeChanged, this, [typeSeparator, this]() {
    typeSeparator->setVisible(m_mode == ViewWindowMode::Edit);
  });

  addAction(toolBar, ViewWindowToolBarHelper2::TypeHeading);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeBold);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeItalic);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeStrikethrough);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeMark);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeUnorderedList);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeOrderedList);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeTodoList);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeCheckedTodoList);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeCode);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeCodeBlock);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeMath);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeMathBlock);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeQuote);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeLink);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeImage);
  addAction(toolBar, ViewWindowToolBarHelper2::TypeTable);

  // Right-side actions: spacer + live preview + layout toggle + find-and-replace.
  ViewWindowToolBarHelper2::addSpacer(toolBar);

  // Live preview toggle (visible only in Edit mode).
  {
    auto *livePreviewAction = addAction(toolBar, ViewWindowToolBarHelper2::ToggleLivePreview);
    livePreviewAction->setChecked(
        m_editViewMode == MarkdownEditorConfig::EditViewMode::EditPreview);
    connect(livePreviewAction, &QAction::toggled, this, [this](bool p_checked) {
      if (m_mode != ViewWindowMode::Edit) {
        return;
      }
      auto mode = p_checked ? MarkdownEditorConfig::EditViewMode::EditPreview
                            : MarkdownEditorConfig::EditViewMode::EditOnly;
      setEditViewMode(mode);
    });
  }

  addAction(toolBar, ViewWindowToolBarHelper2::ToggleLayoutMode);

  addAction(toolBar, ViewWindowToolBarHelper2::FindAndReplace);

  // Print (Markdown-specific: uses viewer for PDF rendering).
  {
    auto *printAction = addAction(toolBar, ViewWindowToolBarHelper2::Print);
    connect(printAction, &QAction::triggered, this, [this]() {
      if (!m_viewer || !m_viewerReady) {
        return;
      }
      m_printer = PrintUtils::promptForPrint(m_viewer->hasSelection(), this);
      if (m_printer) {
        m_printer->setOutputFormat(QPrinter::PdfFormat);
        m_viewer->print(m_printer.get());
      }
    });
  }
}

// ============ setupTextEditor (lazy init) ============

void MarkdownViewWindow2::setupTextEditor() {
  Q_ASSERT(!m_editor);
  Q_ASSERT(m_viewer);  // Viewer must exist before editor (for preview pipeline).

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mdConfig = editorConfig.getMarkdownEditorConfig();

  m_editorController->checkAndUpdateConfigRevision();

  auto *themeService = getServices().get<ThemeService>();
  auto themeFile = themeService->getFile(Theme::File::MarkdownEditorStyle);
  auto syntaxTheme = themeService->getEditorHighlightTheme();
  qreal scaleFactor = WidgetUtils::calculateScaleFactor();

  // Create editor using ServiceLocator constructor.
  m_editor = new MarkdownEditor(
      getServices(),
      mdConfig,
      MarkdownEditorController::buildMarkdownEditorConfig(
          editorConfig, mdConfig, themeFile, syntaxTheme, scaleFactor),
      MarkdownEditorController::buildMarkdownEditorParameters(editorConfig, mdConfig),
      this);

  // Insert at index 0 in splitter (editor always first).
  m_splitter->insertWidget(0, m_editor);

  // Connect editor signals.
  connectEditorSignals();

  // Status widget for editor.
  m_textEditorStatusWidget = m_editor->statusWidget();
  m_mainStatusWidget->addWidget(m_textEditorStatusWidget.get());
  m_textEditorStatusWidget->show();

  // Wire PreviewHelper <-> Editor.
  m_previewHelper->setMarkdownEditor(m_editor);
  m_editor->setPreviewHelper(m_previewHelper);

  // Set content path and base path from Buffer2.
  // contentPath: used by MarkdownEditor::getRelativeLink() for generating relative links.
  // basePath: used by vtextedit's PreviewMgr for resolving relative URLs to absolute paths.
  auto resolved = getBuffer().resolvedPath();
  if (!resolved.isEmpty()) {
    const auto parentDir = QFileInfo(resolved).path();
    m_editor->setContentPath(parentDir);
    m_editor->setBasePath(parentDir);
  }

  // Provide Buffer2 handle for asset/attachment operations.
  m_editor->setBuffer2(&getBuffer());

  // Apply config.
  updateEditorFromConfig();

  // Connect viewer <-> editor web channel signals.
  connect(adapter(), &MarkdownViewerAdapter::ready, m_editor->getHighlighter(),
          &vte::PegMarkdownHighlighter::updateHighlight);
  connect(m_editor, &MarkdownEditor::htmlToMarkdownRequested, adapter(),
          &MarkdownViewerAdapter::htmlToMarkdownRequested);
  connect(adapter(), &MarkdownViewerAdapter::htmlToMarkdownReady, m_editor,
          &MarkdownEditor::handleHtmlToMarkdownData);

  // External code block highlighting pipeline.
  connect(m_editor, &MarkdownEditor::externalCodeBlockHighlightRequested,
          this, &MarkdownViewWindow2::handleExternalCodeBlockHighlightRequest);
  connect(adapter(), &MarkdownViewerAdapter::highlightCodeBlockReady,
          m_editor, &MarkdownEditor::handleExternalCodeBlockHighlightData);

  // Switch to read mode when editor requests it.
  // Save first to avoid losing unsaved changes (legacy: read(true) calls save).
  connect(m_editor, &MarkdownEditor::readRequested, this, [this]() {
    if (save()) {
      setMode(ViewWindowMode::Read);
    }
  });
}

void MarkdownViewWindow2::setupViewer() {
  Q_ASSERT(!m_viewer);

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mdConfig = editorConfig.getMarkdownEditorConfig();

  m_editorController->checkAndUpdateConfigRevision();

  // Update HTML template via HtmlTemplateService.
  auto *htmlTemplateService = getServices().get<HtmlTemplateService>();
  auto *themeService = getServices().get<ThemeService>();
  htmlTemplateService->updateMarkdownViewerTemplate(
      mdConfig,
      themeService->getFile(Theme::File::WebStyleSheet),
      themeService->getFile(Theme::File::HighlightStyleSheet));

  // Create adapter and viewer.
  auto *adapterObj = new MarkdownViewerAdapter(getServices(), this);

  auto bgColor = themeService->getBaseBackground();
  auto zoomFactor = mdConfig.getZoomFactorInReadMode();

  m_viewer = new MarkdownViewer(
      adapterObj, getServices(),
      bgColor,
      zoomFactor,
      this);

  m_splitter->addWidget(m_viewer);
  // Start hidden. The viewer will be shown explicitly when entering
  // Read mode or EditPreview mode. This avoids a visible blank pane
  // in the QSplitter while WebEngine loads the HTML template.
  m_viewer->hide();

  // Viewer status widget.
  {
    auto *label = new QLabel(tr("Markdown Viewer"), this);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_viewerStatusWidget.reset(label);
    m_mainStatusWidget->addWidget(m_viewerStatusWidget.get());
    m_viewerStatusWidget->show();
  }

  m_viewer->setPreviewHelper(m_previewHelper);

  // Zoom persistence.
  connect(m_viewer, &MarkdownViewer::zoomFactorChanged, this, [this](qreal p_factor) {
    m_editorController->persistViewerZoomFactor(p_factor);
    showZoomFactor(p_factor);
  });

  // Link hover status message.
  connect(m_viewer, &WebViewer::linkHovered, this,
          [this](const QString &p_url) { showMessage(p_url); });

  // Edit request from viewer (double-click or context menu).
  connect(m_viewer, &MarkdownViewer::editRequested, this, [this]() {
    setMode(ViewWindowMode::Edit);
  });

  // Viewer find text result.
  connect(adapterObj, &MarkdownViewerAdapter::findTextReady, this,
          [this](const QStringList &p_texts, int p_totalMatches, int p_currentMatchIndex) {
            showFindResult(p_texts, p_totalMatches, p_currentMatchIndex);
          });

  // Viewer ready signal.
  connect(adapterObj, &MarkdownViewerAdapter::ready, this, [this]() {
    m_viewerReady = true;
    if (m_mode == ViewWindowMode::Edit) {
      setEditViewMode(m_editViewMode);
    }
  });

  // Print finished cleanup.
  connect(m_viewer, &MarkdownViewer::printFinished, this, &MarkdownViewWindow2::onPrintFinished);
}

// ============ setupPreviewHelper ============

void MarkdownViewWindow2::setupPreviewHelper() {
  Q_ASSERT(!m_previewHelper);
  m_previewHelper = new PreviewHelper(nullptr, this);

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &mdConfig = configMgr->getEditorConfig().getMarkdownEditorConfig();
  updatePreviewHelperFromConfig(mdConfig);
}

// ============ connectEditorSignals ============

void MarkdownViewWindow2::connectEditorSignals() {
  // Focus forwarding.
  connect(m_editor, &vte::VTextEditor::focusIn, this,
          [this]() { emit focused(this); });

  // Content change -> auto-save dirty flag.
  connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, this, [this]() {
    if (m_propagateEditorToBuffer) {
      onEditorContentsChanged();
    }
  });
}

// ============ setMode / setModeInternal ============

void MarkdownViewWindow2::setMode(ViewWindowMode p_mode) {
  setModeInternal(p_mode, true);
}

void MarkdownViewWindow2::setModeInternal(ViewWindowMode p_mode, bool p_syncBuffer) {
  if (p_mode == m_mode) {
    return;
  }

  // Reentrancy guard: processEvents() below can trigger signals that
  // re-enter this method. Skip if already switching.
  if (m_switchingMode) {
    return;
  }
  m_switchingMode = true;

  // When leaving Edit mode, sync editor content to buffer immediately
  // so the buffer has the latest content for the viewer to read.
  if (m_mode == ViewWindowMode::Edit && m_editor && p_syncBuffer) {
    auto *bufferService = getServices().get<BufferService>();
    if (bufferService) {
      bufferService->syncNow(getBuffer().id());
    }
  }

  m_previousMode = m_mode;
  m_mode = p_mode;

  // Disable propagation during mode switch to avoid false dirty marking.
  m_propagateEditorToBuffer = false;

  auto transition = MarkdownViewWindowController::computeModeTransition(
      static_cast<int>(m_previousMode),
      static_cast<int>(m_mode),
      m_editor != nullptr,
      m_viewer != nullptr,
      p_syncBuffer);

  // Lazy init: create viewer if needed.
  if (transition.needSetupViewer) {
    setupViewer();

    if (transition.needSetupEditor) {
      // Going to Edit mode: briefly show the viewer so WebEngine can
      // initialize with the correct DPI and rendering context. Without
      // this, WebEngine loads very slowly in the background and the
      // ready signal fires much later, causing a disruptive
      // ensureCursorVisible() scroll when the deferred updateHighlight
      // completes.  The viewer will be hidden by setEditViewMode()
      // or explicitly below once the editor is shown.
      // (Matches legacy MarkdownViewWindow behavior.)
      m_viewer->show();
    }
  }

  // Lazy init: create editor if needed.
  if (transition.needSetupEditor) {
    // In Edit mode, we need the viewer for preview pipeline.
    setupTextEditor();
  }

  // Sync content from buffer to the target view.
  if (transition.syncEditorFromBuffer) {
    syncTextEditorFromBuffer(transition.syncPositionFromPrevMode);
  }
  if (transition.syncViewerFromBuffer) {
    syncViewerFromBuffer(transition.syncPositionFromPrevMode);
  }

  // Show/hide widgets and set focus.
  switch (m_mode) {
  case ViewWindowMode::Read:
    m_viewer->show();
    m_viewer->setFocus();
    if (m_editor) {
      m_editor->hide();
      // Hide splitter handle to avoid visible border in readable-width mode.
      m_splitter->handle(1)->setVisible(false);
    }
    m_mainStatusWidget->setCurrentWidget(m_viewerStatusWidget.get());
    break;

  case ViewWindowMode::Edit:
    m_editor->show();
    m_editor->setFocus();
    if (transition.restoreEditViewMode) {
      setEditViewMode(m_editViewMode);
    } else {
      // First time entering edit: apply config view mode.
      auto *configMgr = getServices().get<ConfigMgr2>();
      const auto &mdConfig = configMgr->getEditorConfig().getMarkdownEditorConfig();
      setEditViewMode(MarkdownViewWindowController::getEditViewMode(mdConfig));
    }
    m_mainStatusWidget->setCurrentWidget(m_textEditorStatusWidget.get());
    break;

  default:
    Q_ASSERT(false);
    break;
  }

  // Let widgets show before scrolling (processEvents needed for geometry).
  QCoreApplication::processEvents();

  // Post-switch: sync buffer content to active view (content-only, no template reload).
  // Uses revision check to skip if already synced during initial setup.
  if (transition.syncBufferToActiveView) {
    auto bufferRevision = getBuffer().isValid() ? getBuffer().getRevision() : 0;
    switch (m_mode) {
    case ViewWindowMode::Read:
      if (m_viewer && m_viewerBufferRevision != bufferRevision) {
        auto state = MarkdownEditorController::prepareBufferState(getBuffer());
        if (state.valid) {
          int lineNumber = transition.syncPositionFromPrevMode ? getEditLineNumber() : -1;
          adapter()->setText(state.revision, state.content, lineNumber);
          m_viewerBufferRevision = state.revision;
        }
      } else if (m_viewer && transition.syncPositionFromPrevMode) {
        // Already synced content, but still need to sync scroll position.
        adapter()->scrollToPosition(
            MarkdownViewerAdapter::Position(getEditLineNumber(), QString()));
      }
      break;
    case ViewWindowMode::Edit:
      if (m_editor && m_textEditorBufferRevision != bufferRevision) {
        syncTextEditorFromBuffer(transition.syncPositionFromPrevMode);
      } else if (m_editor && transition.syncPositionFromPrevMode) {
        m_editor->scrollToLine(getReadLineNumber(), false);
      }
      break;
    default:
      break;
    }
  }

  m_switchingMode = false;

  // Enable editor-to-buffer propagation now that initial sync is done.
  // This must happen after all sync calls to avoid marking the buffer dirty
  // from the initial content load.
  if (m_mode == ViewWindowMode::Edit && m_editor) {
    m_propagateEditorToBuffer = true;
  }

  emit modeChanged();

  // Update find-and-replace replace-enabled state on mode switch.
  if (findAndReplaceWidgetVisible()) {
    setFindAndReplaceReplaceEnabled(!isReadMode());
  }
}

// ============ Sync Paths ============

// Path 1: Buffer -> Editor.
void MarkdownViewWindow2::syncTextEditorFromBuffer(bool p_syncPositionFromReadMode) {
  if (!m_editor) {
    return;
  }

  const bool old = m_propagateEditorToBuffer;
  m_propagateEditorToBuffer = false;

  auto state = MarkdownEditorController::prepareBufferState(getBuffer());
  if (state.valid) {
    m_editor->setReadOnly(state.readOnly);
    m_editor->setContentPath(state.basePath);
    m_editor->setBasePath(state.basePath);
    m_editor->setText(state.content);
    m_editor->setModified(state.modified);

    int lineNumber = -1;
    if (p_syncPositionFromReadMode) {
      lineNumber = getReadLineNumber();
    }
    m_editor->scrollToLine(lineNumber, false);
  } else {
    m_editor->setReadOnly(true);
    m_editor->setContentPath(QString());
    m_editor->setBasePath(QString());
    m_editor->setText(QString());
    m_editor->setModified(false);
  }

  m_textEditorBufferRevision = state.revision;
  m_propagateEditorToBuffer = old;
}

// Path 2: Buffer -> Viewer.
void MarkdownViewWindow2::syncViewerFromBuffer(bool p_syncPositionFromEditMode) {
  if (!m_viewer) {
    return;
  }

  auto state = MarkdownEditorController::prepareBufferState(getBuffer());
  if (state.valid) {
    int lineNumber = -1;
    if (p_syncPositionFromEditMode) {
      lineNumber = getEditLineNumber();
    }

    auto *htmlTemplateService = getServices().get<HtmlTemplateService>();
    auto tmpl = htmlTemplateService->getMarkdownViewerTemplate();
    auto baseUrl = PathUtils::pathToUrl(getBuffer().resolvedPath());

    adapter()->reset();
    m_viewer->setHtml(tmpl, baseUrl);
    adapter()->setText(state.revision, state.content, lineNumber);
  } else {
    adapter()->reset();
    m_viewer->setHtml(QString());
    adapter()->setText(0, QString(), -1);
  }
  m_viewerBufferRevision = state.revision;
}

// Path 4: Editor -> Viewer (debounced preview).
void MarkdownViewWindow2::syncEditorContentsToPreview() {
  if (!m_viewerReady || isReadMode() ||
      m_editViewMode == MarkdownEditorConfig::EditViewMode::EditOnly) {
    return;
  }
  // NOTE: Do NOT update m_viewerBufferRevision here.
  // This is visual preview sync only, not a buffer sync event.
  adapter()->setText(m_editor->getText(), m_editor->getTopLine());
}

void MarkdownViewWindow2::syncEditorPositionToPreview() {
  if (!m_viewerReady || isReadMode() ||
      m_editViewMode == MarkdownEditorConfig::EditViewMode::EditOnly) {
    return;
  }
  adapter()->scrollToPosition(
      MarkdownViewerAdapter::Position(m_editor->getTopLine(), QString()));
}

// ViewWindow2 pure virtual override.
void MarkdownViewWindow2::syncEditorFromBuffer() {
  syncTextEditorFromBuffer(false);
  syncViewerFromBuffer(false);
}

// ============ Content ============

QString MarkdownViewWindow2::getLatestContent() const {
  if (m_editor) {
    return m_editor->getText();
  }
  return QString();
}

void MarkdownViewWindow2::setModified(bool p_modified) {
  if (m_editor) {
    m_editor->setModified(p_modified);
  }
}

// ============ Scroll / Zoom ============

void MarkdownViewWindow2::scrollUp() {
  if (isReadMode()) {
    if (adapter()) {
      adapter()->scroll(true);
    }
  } else if (m_editor) {
    auto *vbar = m_editor->getTextEdit()->verticalScrollBar();
    if (vbar && (vbar->minimum() != vbar->maximum())) {
      vbar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
    }
  }
}

void MarkdownViewWindow2::scrollDown() {
  if (isReadMode()) {
    if (adapter()) {
      adapter()->scroll(false);
    }
  } else if (m_editor) {
    auto *vbar = m_editor->getTextEdit()->verticalScrollBar();
    if (vbar && (vbar->minimum() != vbar->maximum())) {
      vbar->triggerAction(QAbstractSlider::SliderSingleStepSub);
    }
  }
}

void MarkdownViewWindow2::zoom(bool p_zoomIn) {
  if (isReadMode()) {
    // Viewer zoom is handled by WebEngine (Ctrl+wheel in web view).
    // Zoom factor change is persisted via zoomFactorChanged signal (wired in setupViewer).
    return;
  }
  if (m_editor) {
    m_editor->zoom(m_editor->zoomDelta() + (p_zoomIn ? 1 : -1));
    int delta = m_editorController->persistZoomDelta(m_editor->zoomDelta());
    showZoomDelta(delta);
  }
}

// ============ selectedText ============

QString MarkdownViewWindow2::selectedText() const {
  switch (m_mode) {
  case ViewWindowMode::Read:
    if (m_viewer) {
      return m_viewer->selectedText();
    }
    break;
  case ViewWindowMode::Edit:
    if (m_editor) {
      return m_editor->getTextEdit()->selectedText();
    }
    break;
  default:
    break;
  }
  return QString();
}

// ============ Editor Config Change ============

void MarkdownViewWindow2::handleEditorConfigChange() {
  // Always update layout mode (WidgetConfig changes don't affect editor config revision).
  ViewWindow2::handleEditorConfigChange();

  if (!m_editorController->checkAndUpdateConfigRevision()) {
    return;
  }

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mdConfig = editorConfig.getMarkdownEditorConfig();

  updatePreviewHelperFromConfig(mdConfig);

  // Update HTML template.
  auto *htmlTemplateService = getServices().get<HtmlTemplateService>();
  auto *themeService = getServices().get<ThemeService>();
  htmlTemplateService->updateMarkdownViewerTemplate(
      mdConfig,
      themeService->getFile(Theme::File::WebStyleSheet),
      themeService->getFile(Theme::File::HighlightStyleSheet));

  if (m_editor) {
    auto themeFile = themeService->getFile(Theme::File::MarkdownEditorStyle);
    auto syntaxTheme = themeService->getEditorHighlightTheme();
    qreal scaleFactor = WidgetUtils::calculateScaleFactor();
    auto config = MarkdownEditorController::buildMarkdownEditorConfig(
        editorConfig, mdConfig, themeFile, syntaxTheme, scaleFactor);
    m_editor->setConfig(config);
    m_editor->updateFromConfig();
    updateEditorFromConfig();
  }

  updateWebViewerConfig();
}

void MarkdownViewWindow2::updateEditorFromConfig() {
  Q_ASSERT(m_editor);
  auto snapshot = m_editorController->currentEditorConfig();
  if (snapshot.zoomDelta != 0) {
    m_editor->zoom(snapshot.zoomDelta);
  }
  vte::Key leaderKey(snapshot.shortcutLeaderKey);
  m_editor->setLeaderKeyToSkip(leaderKey.m_key, leaderKey.m_modifiers);
}

void MarkdownViewWindow2::updatePreviewHelperFromConfig(const MarkdownEditorConfig &p_mdConfig) {
  auto phConfig = MarkdownEditorController::getPreviewHelperConfig(p_mdConfig);
  m_previewHelper->setWebPlantUmlEnabled(phConfig.webPlantUmlEnabled);
  m_previewHelper->setWebGraphvizEnabled(phConfig.webGraphvizEnabled);
  m_previewHelper->setInplacePreviewCodeBlocksEnabled(phConfig.inplacePreviewCodeBlocksEnabled);
  m_previewHelper->setInplacePreviewMathBlocksEnabled(phConfig.inplacePreviewMathBlocksEnabled);
}

void MarkdownViewWindow2::updateWebViewerConfig() {
  if (!m_viewer) {
    return;
  }
  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &mdConfig = configMgr->getEditorConfig().getMarkdownEditorConfig();
  m_viewer->setZoomFactor(mdConfig.getZoomFactorInReadMode());
}

// ============ Edit View Mode ============

void MarkdownViewWindow2::setEditViewMode(MarkdownEditorConfig::EditViewMode p_mode) {
  Q_ASSERT(m_mode == ViewWindowMode::Edit);
  bool modeChanged = false;
  if (m_editViewMode != p_mode) {
    m_editViewMode = p_mode;
    modeChanged = true;
  }

  switch (p_mode) {
  case MarkdownEditorConfig::EditViewMode::EditOnly:
    // Hide splitter handle to avoid visible border in readable-width mode.
    if (m_splitter->count() > 1) {
      m_splitter->handle(1)->setVisible(false);
    }
    // Always hide viewer in EditOnly mode. Even if the viewer is still
    // loading (m_viewerReady == false), hide it immediately to avoid a
    // visible blank pane in the QSplitter while the WebEngine initializes.
    m_viewer->hide();
    if (modeChanged) {
      if (m_syncPreviewTimer) {
        disconnect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged,
                   m_syncPreviewTimer, QOverload<>::of(&QTimer::start));
      }
      disconnect(m_editor, &MarkdownEditor::topLineChanged,
                 this, &MarkdownViewWindow2::syncEditorPositionToPreview);
    }
    break;

  case MarkdownEditorConfig::EditViewMode::EditPreview:
    // Restore splitter handle for draggable split.
    if (m_splitter->count() > 1) {
      m_splitter->handle(1)->setVisible(true);
    }
    m_viewer->show();
    WidgetUtils::distributeWidgetsOfSplitter(m_splitter);
    if (modeChanged) {
      if (!m_syncPreviewTimer) {
        m_syncPreviewTimer = new QTimer(this);
        m_syncPreviewTimer->setSingleShot(true);
        m_syncPreviewTimer->setInterval(
            MarkdownViewWindowController::previewSyncIntervalMs());
        connect(m_syncPreviewTimer, &QTimer::timeout, this,
                &MarkdownViewWindow2::syncEditorContentsToPreview);
      }
      connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged,
              m_syncPreviewTimer, QOverload<>::of(&QTimer::start), Qt::UniqueConnection);
      connect(m_editor, &MarkdownEditor::topLineChanged, this,
              &MarkdownViewWindow2::syncEditorPositionToPreview, Qt::UniqueConnection);
    }
    syncEditorContentsToPreview();
    break;

  default:
    Q_ASSERT(false);
    break;
  }
}

// ============ Find and Replace ============

void MarkdownViewWindow2::handleFindTextChanged(const QString &p_text, FindOptions p_options) {
  if (isReadMode()) {
    if (p_options & FindOption::IncrementalSearch) {
      adapter()->findText(QStringList(p_text), p_options);
    }
  } else {
    TextViewWindowHelper::handleFindTextChanged(this, p_text, p_options);
  }
}

void MarkdownViewWindow2::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  if (isReadMode()) {
    adapter()->findText(p_texts, p_options);
  } else {
    TextViewWindowHelper::handleFindNext(this, p_texts, p_options);
  }
}

void MarkdownViewWindow2::handleReplace(const QString &p_text, FindOptions p_options,
                                         const QString &p_replaceText) {
  if (isReadMode()) {
    showMessage(tr("Replace is not supported in read mode"));
  } else {
    TextViewWindowHelper::handleReplace(this, p_text, p_options, p_replaceText);
  }
}

void MarkdownViewWindow2::handleReplaceAll(const QString &p_text, FindOptions p_options,
                                            const QString &p_replaceText) {
  if (isReadMode()) {
    showMessage(tr("Replace is not supported in read mode"));
  } else {
    TextViewWindowHelper::handleReplaceAll(this, p_text, p_options, p_replaceText);
  }
}

void MarkdownViewWindow2::handleFindAndReplaceWidgetClosed() {
  if (isReadMode()) {
    adapter()->findText(QStringList(), FindOption::FindNone);
  } else {
    TextViewWindowHelper::clearSearchHighlights(this);
  }
}

void MarkdownViewWindow2::handleFindAndReplaceWidgetOpened() {
  // Disable replace in Read mode.
  setFindAndReplaceReplaceEnabled(!isReadMode());
}

// ============ Focus ============

void MarkdownViewWindow2::focusEditor() {
  switch (m_mode) {
  case ViewWindowMode::Read:
    if (m_viewer) {
      m_viewer->setFocus();
    }
    break;
  case ViewWindowMode::Edit:
    if (m_editor) {
      m_editor->setFocus();
    }
    break;
  default:
    break;
  }
}

// ============ Word Count ============

void MarkdownViewWindow2::fetchWordCountInfo(
    const std::function<void(const WordCountInfo &)> &p_callback) const {
  auto text = selectedText();
  if (!text.isEmpty()) {
    auto info = WordCountPanel::calculateWordCount(text);
    info.m_isSelection = true;
    p_callback(info);
    return;
  }

  switch (m_mode) {
  case ViewWindowMode::Read: {
    Q_ASSERT(m_viewer);
    m_viewer->saveContent([p_callback](const QString &p_content) {
      auto info = WordCountPanel::calculateWordCount(p_content);
      p_callback(info);
    });
    break;
  }

  case ViewWindowMode::Edit: {
    auto info = WordCountPanel::calculateWordCount(getLatestContent());
    p_callback(info);
    break;
  }

  default:
    p_callback(WordCountInfo());
    break;
  }
}

// ============ Type Actions ============

void MarkdownViewWindow2::handleTypeAction(int p_action) {
  if (isReadMode() || !m_editor) {
    return;
  }
  switch (p_action) {
  case TypeHeadingNone: m_editor->typeHeading(0); break;
  case TypeHeading1: m_editor->typeHeading(1); break;
  case TypeHeading2: m_editor->typeHeading(2); break;
  case TypeHeading3: m_editor->typeHeading(3); break;
  case TypeHeading4: m_editor->typeHeading(4); break;
  case TypeHeading5: m_editor->typeHeading(5); break;
  case TypeHeading6: m_editor->typeHeading(6); break;
  case TypeBold: m_editor->typeBold(); break;
  case TypeItalic: m_editor->typeItalic(); break;
  case TypeStrikethrough: m_editor->typeStrikethrough(); break;
  case TypeMark: m_editor->typeMark(); break;
  case TypeUnorderedList: m_editor->typeUnorderedList(); break;
  case TypeOrderedList: m_editor->typeOrderedList(); break;
  case TypeTodoList: m_editor->typeTodoList(false); break;
  case TypeCheckedTodoList: m_editor->typeTodoList(true); break;
  case TypeCode: m_editor->typeCode(); break;
  case TypeCodeBlock: m_editor->typeCodeBlock(); break;
  case TypeMath: m_editor->typeMath(); break;
  case TypeMathBlock: m_editor->typeMathBlock(); break;
  case TypeQuote: m_editor->typeQuote(); break;
  case TypeLink: m_editor->typeLink(); break;
  case TypeImage: m_editor->typeImage(); break;
  case TypeTable: m_editor->typeTable(); break;
  default: qWarning() << "Unknown type action" << p_action; break;
  }
}

// ============ Helpers ============

bool MarkdownViewWindow2::eventFilter(QObject *p_obj, QEvent *p_event) {
  if (p_obj == m_splitter) {
    if (p_event->type() == QEvent::FocusIn) {
      focusEditor();
    }
  }

  return ViewWindow2::eventFilter(p_obj, p_event);
}

void MarkdownViewWindow2::handleExternalCodeBlockHighlightRequest(int p_idx, quint64 p_timeStamp,
                                                                   const QString &p_text) {
  static bool stylesInitialized = false;
  if (!stylesInitialized) {
    stylesInitialized = true;
    auto *themeService = getServices().get<ThemeService>();
    const auto file = themeService->getFile(Theme::File::HighlightStyleSheet);
    if (file.isEmpty()) {
      qWarning() << "no highlight style sheet specified for external code block highlight";
    } else {
      QString content;
      try {
        content = FileUtils::readTextFile(file);
      } catch (Exception &e) {
        qWarning() << "failed to read highlight style sheet for external code block highlight"
                   << file << e.what();
      }
      adapter()->fetchStylesFromStyleSheet(
          content, [this](const QVector<MarkdownViewerAdapter::CssRuleStyle> *rules) {
            MarkdownEditor::ExternalCodeBlockHighlightStyles styles;

            const QString prefix(".token.");
            for (const auto &rule : *rules) {
              bool isFirst = true;
              QTextCharFormat fmt;

              // Just fetch `.token.attr` styles.
              auto selects = rule.m_selector.split(QLatin1Char(','));
              for (const auto &sel : selects) {
                const auto ts = sel.trimmed();
                if (!ts.startsWith(prefix)) {
                  continue;
                }
                auto classList = ts.mid(prefix.size()).split(QLatin1Char('.'));
                for (const auto &cla : classList) {
                  if (isFirst) {
                    fmt = rule.toTextCharFormat();
                    isFirst = false;
                  }
                  styles.insert(cla, fmt);
                }
              }
            }

            MarkdownEditor::setExternalCodeBlockHighlihgtStyles(styles);
          });
    }
  }

  adapter()->highlightCodeBlock(p_idx, p_timeStamp, p_text);
}

void MarkdownViewWindow2::onPrintFinished(bool p_succeeded) {
  m_printer.reset();
  showMessage(p_succeeded ? tr("Printed to PDF") : tr("Failed to print to PDF"));
}

MarkdownViewerAdapter *MarkdownViewWindow2::adapter() const {
  if (m_viewer) {
    return m_viewer->adapter();
  }
  return nullptr;
}

int MarkdownViewWindow2::getEditLineNumber() const {
  if (m_previousMode == ViewWindowMode::Edit && m_editor) {
    return m_editor->getTopLine();
  }
  return -1;
}

int MarkdownViewWindow2::getReadLineNumber() const {
  if (m_previousMode == ViewWindowMode::Read && m_viewer) {
    return adapter()->getTopLineNumber();
  }
  return -1;
}

bool MarkdownViewWindow2::isReadMode() const {
  return m_mode == ViewWindowMode::Read;
}

int MarkdownViewWindow2::getCursorPosition() const {
  if (m_mode == ViewWindowMode::Edit && m_editor) {
    return m_editor->getTextEdit()->textCursor().blockNumber();
  }
  return -1;
}

int MarkdownViewWindow2::getScrollPosition() const {
  if (m_mode == ViewWindowMode::Edit && m_editor) {
    return m_editor->getTextEdit()->verticalScrollBar()->value();
  }
  return -1;
}
