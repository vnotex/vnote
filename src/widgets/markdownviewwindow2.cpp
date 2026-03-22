#include "markdownviewwindow2.h"

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QPrinter>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

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
#include "wordcountpanel.h"
#include "wordcountpopup2.h"

using namespace vnotex;

typedef EditorConfig::Shortcut Shortcut;

// ============ Constructor ============

MarkdownViewWindow2::MarkdownViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                         QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  // Start at Invalid so the first setMode() triggers full initialization.
  m_mode = ViewWindowMode::Invalid;
  m_editorController = new MarkdownEditorController(p_services, this);
  m_windowController = new MarkdownViewWindowController(p_services, this);

  setupUI();
  setupPreviewHelper();

  // Trigger initial mode setup (creates viewer, loads content).
  setModeInternal(ViewWindowMode::Read, true);
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

  auto *configMgr = getServices().get<ConfigMgr2>();
  Q_ASSERT(configMgr);
  const auto &editorConfig = configMgr->getEditorConfig();

  // 1. EditRead toggle.
  m_editReadAction = ViewWindowToolBarHelper2::addAction(
      toolBar, ViewWindowToolBarHelper2::EditRead, getServices(), this);
  // Checked = Edit mode, Unchecked = Read mode.
  m_editReadAction->setChecked(m_mode == ViewWindowMode::Edit);
  connect(m_editReadAction, &QAction::toggled, this, [this](bool p_checked) {
    setMode(p_checked ? ViewWindowMode::Edit : ViewWindowMode::Read);
  });
  connect(this, &ViewWindow2::modeChanged, this, [this]() {
    QSignalBlocker blocker(m_editReadAction);
    m_editReadAction->setChecked(m_mode == ViewWindowMode::Edit);
    // Update icon based on mode.
    if (m_mode == ViewWindowMode::Edit) {
      m_editReadAction->setIcon(
          ViewWindowToolBarHelper2::generateIcon(getServices(), QStringLiteral("read_editor.svg")));
    } else {
      m_editReadAction->setIcon(
          ViewWindowToolBarHelper2::generateIcon(getServices(), QStringLiteral("edit_editor.svg")));
    }
  });

  // 2. Save.
  m_saveAction = ViewWindowToolBarHelper2::addAction(
      toolBar, ViewWindowToolBarHelper2::Save, getServices(), this);
  m_saveAction->setEnabled(false);
  connect(m_saveAction, &QAction::triggered, this, [this]() { save(); });
  connect(this, &ViewWindow2::statusChanged, this, [this]() {
    m_saveAction->setEnabled(getBuffer().isValid() && isModified());
  });

  // 3. Word count popup.
  {
    auto *wcAction = ViewWindowToolBarHelper2::addAction(
        toolBar, ViewWindowToolBarHelper2::WordCount, getServices(), this);
    auto *toolBtn = dynamic_cast<QToolButton *>(toolBar->widgetForAction(wcAction));
    Q_ASSERT(toolBtn);
    auto *popup = new WordCountPopup2(
        toolBtn,
        [this](WordCountPanel *p_panel) {
          QString text;
          bool isSelection = false;
          if (m_editor && !isReadMode()) {
            text = m_editor->getTextEdit()->selectedText();
            isSelection = !text.isEmpty();
          }
          if (!isSelection) {
            text = isReadMode() ? QString() : getLatestContent();
          }
          auto info = WordCountPanel::calculateWordCount(text);
          p_panel->updateCount(isSelection, info.m_wordCount,
                               info.m_charWithoutSpaceCount, info.m_charWithSpaceCount);
        },
        toolBar);
    toolBtn->setMenu(popup);
  }

  toolBar->addSeparator();

  // 4. Type* formatting actions.
  // Helper lambda to create a formatting action.
  auto addTypeAction = [&](const QString &p_iconName, const QString &p_text,
                           const QString &p_shortcut, int p_actionId) -> QAction * {
    auto *act = toolBar->addAction(
        ViewWindowToolBarHelper2::generateIcon(getServices(), p_iconName), p_text);
    act->setEnabled(false);
    if (!p_shortcut.isEmpty()) {
      ViewWindowToolBarHelper2::addActionShortcut(act, p_shortcut, this);
    }
    connect(act, &QAction::triggered, this, [this, p_actionId]() { handleTypeAction(p_actionId); });
    connect(this, &ViewWindow2::modeChanged, this, [act, this]() {
      act->setEnabled(m_mode == ViewWindowMode::Edit);
    });
    return act;
  };

  // Heading submenu with H1-H6 + HeadingNone.
  {
    auto *headingBtn = new QToolButton(toolBar);
    headingBtn->setIcon(
        ViewWindowToolBarHelper2::generateIcon(getServices(), QStringLiteral("type_heading_editor.svg")));
    headingBtn->setToolTip(tr("Heading"));
    headingBtn->setPopupMode(QToolButton::InstantPopup);
    headingBtn->setEnabled(false);

    auto *headingMenu = new QMenu(headingBtn);
    for (int level = 1; level <= 6; ++level) {
      auto *act = headingMenu->addAction(tr("Heading %1").arg(level));
      auto shortcutKey = editorConfig.getShortcut(
          static_cast<Shortcut>(Shortcut::TypeHeading1 + level - 1));
      if (!shortcutKey.isEmpty()) {
        ViewWindowToolBarHelper2::addActionShortcut(act, shortcutKey, this);
      }
      connect(act, &QAction::triggered, this, [this, level]() { handleTypeAction(level); });
    }
    headingMenu->addSeparator();
    {
      auto *noneAct = headingMenu->addAction(tr("Clear"));
      auto shortcutKey = editorConfig.getShortcut(Shortcut::TypeHeadingNone);
      if (!shortcutKey.isEmpty()) {
        ViewWindowToolBarHelper2::addActionShortcut(noneAct, shortcutKey, this);
      }
      connect(noneAct, &QAction::triggered, this, [this]() { handleTypeAction(TypeHeadingNone); });
    }
    headingBtn->setMenu(headingMenu);
    toolBar->addWidget(headingBtn);

    connect(this, &ViewWindow2::modeChanged, this, [headingBtn, this]() {
      headingBtn->setEnabled(m_mode == ViewWindowMode::Edit);
    });
  }

  addTypeAction(QStringLiteral("type_bold_editor.svg"), tr("Bold"),
                editorConfig.getShortcut(Shortcut::TypeBold), TypeBold);
  addTypeAction(QStringLiteral("type_italic_editor.svg"), tr("Italic"),
                editorConfig.getShortcut(Shortcut::TypeItalic), TypeItalic);
  addTypeAction(QStringLiteral("type_strikethrough_editor.svg"), tr("Strikethrough"),
                editorConfig.getShortcut(Shortcut::TypeStrikethrough), TypeStrikethrough);
  addTypeAction(QStringLiteral("type_mark_editor.svg"), tr("Mark"),
                editorConfig.getShortcut(Shortcut::TypeMark), TypeMark);
  addTypeAction(QStringLiteral("type_unordered_list_editor.svg"), tr("Unordered List"),
                editorConfig.getShortcut(Shortcut::TypeUnorderedList), TypeUnorderedList);
  addTypeAction(QStringLiteral("type_ordered_list_editor.svg"), tr("Ordered List"),
                editorConfig.getShortcut(Shortcut::TypeOrderedList), TypeOrderedList);
  addTypeAction(QStringLiteral("type_todo_list_editor.svg"), tr("Todo List"),
                editorConfig.getShortcut(Shortcut::TypeTodoList), TypeTodoList);
  addTypeAction(QStringLiteral("type_checked_todo_list_editor.svg"), tr("Checked Todo List"),
                editorConfig.getShortcut(Shortcut::TypeCheckedTodoList), TypeCheckedTodoList);
  addTypeAction(QStringLiteral("type_code_editor.svg"), tr("Code"),
                editorConfig.getShortcut(Shortcut::TypeCode), TypeCode);
  addTypeAction(QStringLiteral("type_code_block_editor.svg"), tr("Code Block"),
                editorConfig.getShortcut(Shortcut::TypeCodeBlock), TypeCodeBlock);
  addTypeAction(QStringLiteral("type_math_editor.svg"), tr("Math"),
                editorConfig.getShortcut(Shortcut::TypeMath), TypeMath);
  addTypeAction(QStringLiteral("type_math_block_editor.svg"), tr("Math Block"),
                editorConfig.getShortcut(Shortcut::TypeMathBlock), TypeMathBlock);
  addTypeAction(QStringLiteral("type_quote_editor.svg"), tr("Quote"),
                editorConfig.getShortcut(Shortcut::TypeQuote), TypeQuote);
  addTypeAction(QStringLiteral("type_link_editor.svg"), tr("Link"),
                editorConfig.getShortcut(Shortcut::TypeLink), TypeLink);
  addTypeAction(QStringLiteral("type_image_editor.svg"), tr("Image"),
                editorConfig.getShortcut(Shortcut::TypeImage), TypeImage);
  addTypeAction(QStringLiteral("type_table_editor.svg"), tr("Table"),
                editorConfig.getShortcut(Shortcut::TypeTable), TypeTable);

  ViewWindowToolBarHelper2::addSpacer(toolBar);

  // 5. FindAndReplace.
  {
    auto *findAction = ViewWindowToolBarHelper2::addAction(
        toolBar, ViewWindowToolBarHelper2::FindAndReplace, getServices(), this);
    connect(findAction, &QAction::triggered, this, [this]() {
      if (findAndReplaceWidgetVisible()) {
        hideFindAndReplaceWidget();
      } else {
        showFindAndReplaceWidget();
      }
    });
  }

  // 6. Print.
  {
    auto *printAction = ViewWindowToolBarHelper2::addAction(
        toolBar, ViewWindowToolBarHelper2::Print, getServices(), this);
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

  // Set content path from Buffer2 (replaces m_editor->setBuffer(buffer)).
  auto resolved = getBuffer().resolvedPath();
  if (!resolved.isEmpty()) {
    m_editor->setContentPath(QFileInfo(resolved).path());
  }

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
    save();
    setMode(ViewWindowMode::Read);
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
  }

  // Lazy init: create editor if needed.
  if (transition.needSetupEditor) {
    // In Edit mode, we need the viewer for preview pipeline.
    // Must show viewer briefly to let it init with correct DPI.
    if (m_viewer && !m_viewer->isVisible()) {
      m_viewer->show();
    }
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
    if (m_viewerReady) {
      m_viewer->hide();
    }
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
