#include "markdownviewwindow2.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QJsonArray>
#include <QMenu>
#include <QPrinter>
#include <QScrollBar>
#include <QSplitter>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

#include <vtextedit/markdownhighlighter.h>
#include <vtextedit/markdownutils.h>
#include <vtextedit/vtextedit.h>

#include <controllers/imagehostcontroller.h>
#include <controllers/markdowneditorcontroller.h>
#include <controllers/markdownviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/htmltemplateservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/workspacecoreservice.h>
#include <core/theme.h>
#include <core/widgetconfig.h>
#include <gui/services/themeservice.h>
#include <gui/utils/printutils.h>
#include <gui/utils/widgetutils.h>
#include <imagehost/iimagehostprovider.h>
#include <imagehost/imagehostpath.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>
#include <utils/urlutils.h>
#include <vxcore/notebook_json_keys.h>

#include "../utils/scrollpreservationpolicy.h"
#include "editors/editorstatusbarbinder.h"
#include "editors/graphvizhelper.h"
#include "editors/markdowneditor.h"
#include "editors/markdownviewer.h"
#include "editors/markdownvieweradapter.h"
#include "editors/plantumlhelper.h"
#include "editors/previewhelper.h"
#include "editors/statusbar.h"
#include "encodingbutton.h"
#include "legacyimagemigrationbar.h"
#include "messageboxhelper.h"
#include "outlinepopup.h"
#include "outlineprovider.h"
#include "textviewwindowhelper.h"
#include "viewwindowtoolbarhelper2.h"

using namespace vnotex;

// ============ Constructor ============

MarkdownViewWindow2::MarkdownViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                         QWidget *p_parent, ViewWindowMode p_initialMode)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  // Start at Invalid so the first setMode() triggers full initialization.
  m_mode = ViewWindowMode::Invalid;
  m_editorController = new MarkdownEditorController(p_services, this);
  m_windowController = new MarkdownViewWindowController(p_services, this);
  m_imageHostController = p_services.get<ImageHostController>();

  setupOutlineProvider();
  setupUI();
  setupPreviewHelper();

  // Trigger initial mode setup directly to the requested mode.
  // Going straight to Edit (Invalid -> Edit) avoids a visible Read -> Edit
  // transition (flash of blank viewer in QSplitter).
  setModeInternal(p_initialMode, true);

  setupShortcuts();

  snapshotInitialImages();
}

// ============ Destructor ============

MarkdownViewWindow2::~MarkdownViewWindow2() {
  // Disconnect controller signals to prevent delivery to a destroyed widget.
  if (m_imageHostController) {
    disconnect(m_imageHostController, nullptr, this, nullptr);
  }

  // The StatusBar is a QObject child of the window and the binder is parented to
  // this; both are cleaned up automatically. The encoding button is owned by the
  // bar's Widget-column mount (raw overload).
}

// ============ setupUI ============

void MarkdownViewWindow2::setupUI() {
  // Central widget: splitter to hold editor (index 0) and viewer (index 1).
  m_splitter = new QSplitter(this);
  m_splitter->setContentsMargins(0, 0, 0, 0);
  setCentralWidget(m_splitter);
  // Get the focus event from splitter.
  m_splitter->installEventFilter(this);

  // Column-based status bar. Built eagerly with the editor columns empty (no
  // editor yet in Read mode) plus a trailing encoding column; the binder is
  // attached to the editor later in setupTextEditor().
  {
    m_statusBinder = new EditorStatusBarBinder(this);
    auto def = m_statusBinder->buildDef(); // no editor yet

    int encodingIndex = -1;
    if (isEncodingSupported()) {
      StatusBarColumn encoding;
      encoding.type = StatusBarColumnType::Widget;
      encodingIndex = def.size();
      def << encoding;
    }

    setStatusBarDef(def);
    if (encodingIndex >= 0 && statusBar()) {
      statusBar()->setColumnWidget(encodingIndex, ensureEncodingButton());
    }

    // Read-mode auto-hide: collapse the bar unless a transient message is shown.
    if (statusBar()) {
      connect(statusBar(), &StatusBar::messageVisibilityChanged, this, [this](bool p_visible) {
        if (m_mode == ViewWindowMode::Read && statusBar()) {
          statusBar()->setMaximumHeight(p_visible ? QWIDGETSIZE_MAX : 0);
        }
      });
    }
  }

  setupToolBar();
}

// ============ setupToolBar ============

void MarkdownViewWindow2::setupToolBar() {
  auto *toolBar = createToolBar(this);
  addToolBar(toolBar);

  addAction(toolBar, ViewWindowToolBarHelper2::EditRead);
  addLeftCommonToolBarActions(toolBar);

  // Separator between word count and formatting actions (visible only in edit mode).
  auto *typeSeparator = toolBar->addSeparator();
  typeSeparator->setVisible(false);
  connect(this, &ViewWindow2::modeChanged, this,
          [typeSeparator, this]() { typeSeparator->setVisible(m_mode == ViewWindowMode::Edit); });

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

  addRightCommonToolBarActions(toolBar);
}

void MarkdownViewWindow2::addAdditionalRightToolBarActions(QToolBar *p_toolBar) {
  // Image host selection button.
  {
    auto *act = addAction(p_toolBar, ViewWindowToolBarHelper2::ImageHost);
    auto *btn = qobject_cast<QToolButton *>(p_toolBar->widgetForAction(act));
    if (btn) {
      m_imageHostMenu = btn->menu();
      updateImageHostMenu();
      connect(m_imageHostMenu, &QMenu::triggered, this,
              [this](QAction *p_act) { handleImageHostChanged(p_act->data().toString()); });
    }
    if (m_imageHostController) {
      connect(m_imageHostController, &ImageHostController::providerChanged, this,
              &MarkdownViewWindow2::updateImageHostMenu);
    }

    // Image host button is only relevant while editing.
    act->setVisible(m_mode == ViewWindowMode::Edit);
    connect(this, &ViewWindow2::modeChanged, this,
            [act, this]() { act->setVisible(m_mode == ViewWindowMode::Edit); });
  }

  // Outline popup button (right corner, first): wire it to this window's outline provider.
  {
    auto *outlineAct = addAction(p_toolBar, ViewWindowToolBarHelper2::Outline);
    auto *toolBtn = dynamic_cast<QToolButton *>(p_toolBar->widgetForAction(outlineAct));
    if (toolBtn) {
      auto *outlinePopup = dynamic_cast<OutlinePopup *>(toolBtn->menu());
      if (outlinePopup) {
        outlinePopup->setOutlineProvider(m_outlineProvider);
      }
    }
  }

  // Live preview toggle (visible only in Edit mode).
  {
    auto *livePreviewAction = addAction(p_toolBar, ViewWindowToolBarHelper2::ToggleLivePreview);
    livePreviewAction->setChecked(m_editViewMode ==
                                  MarkdownEditorConfig::EditViewMode::EditPreview);
    connect(livePreviewAction, &QAction::toggled, this, [this](bool p_checked) {
      if (m_mode != ViewWindowMode::Edit) {
        return;
      }
      auto mode = p_checked ? MarkdownEditorConfig::EditViewMode::EditPreview
                            : MarkdownEditorConfig::EditViewMode::EditOnly;
      setEditViewMode(mode);
    });
  }

  // In-place preview toggle (visible only in Edit mode).
  {
    auto *inplacePreviewAction = addAction(p_toolBar, ViewWindowToolBarHelper2::InplacePreview);
    // setChecked before connect so this does not fire the toggled handler.
    inplacePreviewAction->setChecked(m_inplacePreviewEnabled);
    connect(inplacePreviewAction, &QAction::toggled, this, [this](bool p_checked) {
      m_inplacePreviewEnabled = p_checked;
      if (m_mode == ViewWindowMode::Edit && m_editor) {
        m_editor->setInplacePreviewEnabled(p_checked);
      }
    });
  }
}

void MarkdownViewWindow2::handlePrint() {
  if (!m_viewer || !m_viewerReady) {
    return;
  }

  m_printer = PrintUtils::promptForPrint(m_viewer->hasSelection(), this);
  if (m_printer) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    m_viewer->page()->print(m_printer.data(), std::bind(&MarkdownViewWindow2::onPrintFinished, this,
                                                        std::placeholders::_1));
#else
    m_printer->setOutputFormat(QPrinter::PdfFormat);
    m_viewer->print(m_printer.get());
#endif
  }
}

bool MarkdownViewWindow2::aboutToClose(bool p_force) {
  const bool isLast = isLastWindowForBuffer();
  const bool result = ViewWindow2::aboutToClose(p_force);
  if (!result) {
    return false;
  }

  if (isLast) {
    clearObsoleteImages();

    // Order matters. clearObsoleteImages() runs FIRST so that in the undo case
    // it removes the abandoned staged copies; the finalize gate below then
    // fails harmlessly (the old URLs are still on disk) and the originals
    // survive. In the success case it finds nothing obsolete and the finalize
    // removes the originals.
    //
    // Do NOT move this earlier (auto-save / timer driven): deleting before the
    // editor's final state is settled reintroduces the undo-after-delete
    // data-loss path.
    if (!m_pendingLegacyMigration.isEmpty() && m_legacyImageController) {
      auto *bufferSvc = getServices().get<BufferService>();
      if (bufferSvc) {
        const auto bufferId = getBuffer().id();
        m_legacyImageController->finalize(getBuffer(), m_pendingLegacyMigration,
                                          bufferSvc->isDirty(bufferId),
                                          bufferSvc->isSaveQueueBusy(bufferId));
      }
      m_pendingLegacyMigration.clear();
    }
  }

  return true;
}

// ============ setupTextEditor (lazy init) ============

void MarkdownViewWindow2::setupTextEditor() {
  Q_ASSERT(!m_editor);
  Q_ASSERT(m_viewer); // Viewer must exist before editor (for preview pipeline).

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mdConfig = editorConfig.getMarkdownEditorConfig();

  m_editorController->checkAndUpdateConfigRevision();

  auto *themeService = getServices().get<ThemeService>();
  auto themeContent = themeService->fetchTextEditorStyle();
  auto syntaxTheme = themeService->getEditorHighlightTheme();
  qreal scaleFactor = WidgetUtils::calculateScaleFactor();

  const auto &widgetConfig = configMgr->getWidgetConfig();
  int maxContentWidth =
      widgetConfig.getViewWindowLayoutMode() == ViewWindowLayoutMode::ReadableWidth
          ? widgetConfig.getReadableWidthMaxPx()
          : 0;

  // Create editor using ServiceLocator constructor.
  m_editor = new MarkdownEditor(
      getServices(), mdConfig,
      MarkdownEditorController::buildMarkdownEditorConfigFromContent(
          editorConfig, mdConfig, themeContent, syntaxTheme, scaleFactor, maxContentWidth),
      MarkdownEditorController::buildMarkdownEditorParameters(editorConfig, mdConfig), this);

  // Insert at index 0 in splitter (editor always first).
  m_splitter->insertWidget(0, m_editor);

  // Connect editor signals.
  connectEditorSignals();

  // Bind the editor to the status bar columns.
  if (m_statusBinder && statusBar()) {
    m_statusBinder->attach(m_editor, statusBar());
  }

  // Wire PreviewHelper <-> Editor.
  m_previewHelper->setMarkdownEditor(m_editor);
  m_editor->setPreviewHelper(m_previewHelper);

  // Apply the current in-place preview toggle state to the freshly created editor.
  m_editor->setInplacePreviewEnabled(m_inplacePreviewEnabled);

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

  // Provide image host controller for remote image uploads.
  if (m_imageHostController) {
    m_editor->setImageHostController(m_imageHostController);
  }

  // Apply config.
  updateEditorFromConfig();

  // Honour a persisted zoom delta for the in-place previews.
  m_previewHelper->editorZoomChanged();

  // Connect viewer <-> editor web channel signals.
  connect(adapter(), &MarkdownViewerAdapter::ready, m_editor->getHighlighter(),
          &vte::MarkdownHighlighter::updateHighlight);
  connect(m_editor, &MarkdownEditor::htmlToMarkdownRequested, adapter(),
          &MarkdownViewerAdapter::htmlToMarkdownRequested);
  connect(adapter(), &MarkdownViewerAdapter::htmlToMarkdownReady, m_editor,
          &MarkdownEditor::handleHtmlToMarkdownData);

  // External code block highlighting pipeline.
  connect(m_editor, &MarkdownEditor::externalCodeBlockHighlightRequested, this,
          &MarkdownViewWindow2::handleExternalCodeBlockHighlightRequest);
  connect(adapter(), &MarkdownViewerAdapter::highlightCodeBlockReady, m_editor,
          &MarkdownEditor::handleExternalCodeBlockHighlightData);

  // External display math ($$...$$) source highlighting pipeline.
  connect(m_editor, &MarkdownEditor::externalMathHighlightRequested, this,
          &MarkdownViewWindow2::handleExternalMathHighlightRequest);
  connect(adapter(), &MarkdownViewerAdapter::highlightMathReady, m_editor,
          &MarkdownEditor::handleExternalMathHighlightData);

  // Switch to read mode when editor requests it.
  // Save first to avoid losing unsaved changes (legacy: read(true) calls save).
  connect(m_editor, &MarkdownEditor::readRequested, this, [this]() {
    if (save()) {
      setMode(ViewWindowMode::Read);
    }
  });

  // Resolve heading anchors for the edit-mode "Copy Link" action via the web
  // side, so the anchor matches the one read mode renders.
  m_editor->setHeadingLinkResolver([this](const QString &p_text, int p_line,
                                          std::function<void(bool, const QString &)> p_cb) {
    adapter()->fetchHeadingAnchor(
        p_text, p_line, [this, p_cb](const MarkdownViewerAdapter::HeadingAnchorResult &p_result) {
          if (!p_result.m_found) {
            p_cb(false, QString());
            return;
          }
          p_cb(true, MarkdownEditorController::composeHeadingLink(getBuffer().resolvedPath(),
                                                                  p_result.m_anchor));
        });
  });

  applyReadableWidth();
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
      mdConfig, themeService->fetchWebStyleSheet(),
      themeService->getFile(Theme::File::HighlightStyleSheet));

  // Create adapter and viewer.
  auto *adapterObj = new MarkdownViewerAdapter(getServices(), this);

  auto bgColor = getServices().get<ThemeService>()->getBaseBackground();
  auto zoomFactor = mdConfig.getZoomFactorInReadMode();

  m_viewer = new MarkdownViewer(adapterObj, this, getServices(), bgColor, zoomFactor, this);
  m_viewer->setController(m_windowController);

  m_splitter->addWidget(m_viewer);
  // Start hidden. The viewer will be shown explicitly when entering
  // Read mode or EditPreview mode. This avoids a visible blank pane
  // in the QSplitter while WebEngine loads the HTML template.
  m_viewer->hide();

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
  connect(m_viewer, &MarkdownViewer::editRequested, this,
          [this]() { setMode(ViewWindowMode::Edit); });

  // Export request from viewer context menu; forward the inherited signal.
  connect(m_viewer, &MarkdownViewer::exportRequested, this, &MarkdownViewWindow2::exportRequested);

  // Viewer find text result.
  connect(adapterObj, &MarkdownViewerAdapter::findTextReady, this,
          [this](const QStringList &p_texts, int p_totalMatches, int p_currentMatchIndex) {
            showFindResult(p_texts, p_totalMatches, p_currentMatchIndex);
          });

  // Viewer ready signal.
  connect(adapterObj, &MarkdownViewerAdapter::ready, this, [this]() {
    m_viewerReady = true;
    applyReadableWidth();
    if (m_mode == ViewWindowMode::Edit) {
      setEditViewMode(m_editViewMode);
    }
  });

  // Deferred anchor scroll: drain pending anchor after rendering completes.
  connect(adapterObj, &MarkdownViewerAdapter::workFinished, this, [this]() {
    if (!m_pendingAnchor.isEmpty() && isReadMode() && adapter()) {
      adapter()->scrollToPosition(MarkdownViewerAdapter::Position(-1, m_pendingAnchor));
      m_pendingAnchor.clear();
    }
  });

  // Print finished cleanup.
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
  connect(m_viewer, &MarkdownViewer::printFinished, this, &MarkdownViewWindow2::onPrintFinished);
#endif

  // Outline pipeline: viewer headings -> OutlineProvider.
  connect(adapterObj, &MarkdownViewerAdapter::headingsChanged, this, [this]() {
    if (isReadMode()) {
      auto outline = headingsToOutline(adapter()->getHeadings());
      m_outlineProvider->setOutline(outline);
    }
  });
  connect(adapterObj, &MarkdownViewerAdapter::currentHeadingChanged, this, [this]() {
    if (isReadMode()) {
      m_outlineProvider->setCurrentHeadingIndex(adapter()->getCurrentHeadingIndex());
    }
  });

  // Read-mode link interception: open internal files via BufferService with anchor.
  connect(m_viewer, &WebViewer::localFileOpenRequested, this, [this](const QUrl &p_url) {
    if (!p_url.isLocalFile()) {
      return;
    }
    QString localPath = p_url.toLocalFile();
    QString fragment = p_url.fragment();
    QString combined = localPath;
    if (!fragment.isEmpty()) {
      combined += QLatin1Char('#') + fragment;
    }
    handleOpenFileRequest(combined);
  });

  // Read-mode external link interception: route through the same policy sink as
  // edit mode so the security prompt lives in one place.
  connect(m_viewer, &WebViewer::externalLinkRequested, this,
          [this](const QUrl &p_url) { handleOpenFileRequest(p_url.toString()); });
}

// ============ setupPreviewHelper ============

void MarkdownViewWindow2::setupPreviewHelper() {
  Q_ASSERT(!m_previewHelper);
  m_previewHelper = new PreviewHelper(nullptr, this);

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &mdConfig = configMgr->getEditorConfig().getMarkdownEditorConfig();
  updatePreviewHelperFromConfig(mdConfig);
}

// ============ setupOutlineProvider ============

void MarkdownViewWindow2::setupOutlineProvider() {
  m_outlineProvider.reset(new OutlineProvider(nullptr));

  // When the outline heading is clicked, scroll the active view to that heading.
  connect(m_outlineProvider.data(), &OutlineProvider::headingClicked, this, [this](int p_idx) {
    switch (m_mode) {
    case ViewWindowMode::Read:
      if (adapter()) {
        adapter()->scrollToHeading(p_idx);
      }
      break;
    case ViewWindowMode::Edit:
      if (m_editor) {
        m_editor->scrollToHeading(p_idx);
      }
      break;
    default:
      break;
    }
  });
}

QSharedPointer<OutlineProvider> MarkdownViewWindow2::getOutlineProvider() const {
  return m_outlineProvider;
}

template <class T>
QSharedPointer<Outline> MarkdownViewWindow2::headingsToOutline(const QVector<T> &p_headings) {
  auto outline = QSharedPointer<Outline>::create();
  if (!p_headings.isEmpty()) {
    outline->m_headings.reserve(p_headings.size());
    for (const auto &heading : p_headings) {
      outline->m_headings.push_back(Outline::Heading(heading.m_name, heading.m_level));
    }
  }

  return outline;
}

// ============ connectEditorSignals ============

void MarkdownViewWindow2::connectEditorSignals() {
  // Focus forwarding.
  connect(m_editor, &vte::VTextEditor::focusIn, this, [this]() { emit focused(this); });

  // Content change -> auto-save dirty flag.
  connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, this, [this]() {
    if (m_propagateEditorToBuffer) {
      onEditorContentsChanged();
    }
  });

  // Outline pipeline: editor headings -> OutlineProvider.
  connect(m_editor, &MarkdownEditor::headingsChanged, this, [this]() {
    if (!isReadMode()) {
      auto outline = headingsToOutline(m_editor->getHeadings());
      m_outlineProvider->setOutline(outline);
    }
  });
  connect(m_editor, &MarkdownEditor::currentHeadingChanged, this, [this]() {
    if (!isReadMode()) {
      m_outlineProvider->setCurrentHeadingIndex(m_editor->getCurrentHeadingIndex());
    }
  });

  // Snippet apply from context menu / shortcut.
  connect(m_editor, &MarkdownEditor::applySnippetRequested, this,
          QOverload<>::of(&MarkdownViewWindow2::applySnippet));

  // Track newly inserted images for obsolete-image cleanup.
  connect(m_editor, &MarkdownEditor::imageInserted, this,
          [this](const QString &p_imagePath, const QString &p_urlInLink) {
            Q_UNUSED(p_imagePath);
            if (!p_urlInLink.isEmpty()) {
              m_insertedImages.insert(p_urlInLink);
            }
          });

  // Self-file anchor link resolution.
  connect(m_editor, &MarkdownEditor::openFileRequested, this,
          [this](const QString &p_filePath) { handleOpenFileRequest(p_filePath); });
}

void MarkdownViewWindow2::handleOpenFileRequest(const QString &p_filePath) {
  if (p_filePath.startsWith(QLatin1Char('#'))) {
    handleAnchorJump(p_filePath.mid(1));
    return;
  }

  // External links (http/https/ftp/mailto) are opened by the system. This is the
  // single policy sink shared by edit-mode (Ctrl+click, context-menu "Open Link")
  // and read-mode navigation, so the security prompt lives in one place.
  const QUrl url(p_filePath);
  const auto scheme = url.scheme();
  if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https") ||
      scheme == QStringLiteral("ftp") || scheme == QStringLiteral("mailto")) {
    int ret = MessageBoxHelper::questionYesNo(
        MessageBoxHelper::Warning, tr("Are you sure to open link (%1)?").arg(url.toString()),
        tr("Malicious link might do harm to your device."), QString(), this);
    if (ret == QMessageBox::Yes) {
      QDesktopServices::openUrl(url);
    }
    return;
  }

  auto result = splitUrlFragment(p_filePath);
  if (result.path.isEmpty()) {
    return;
  }

  QString basePath = getBuffer().getResourceBasePath();
  QString absPath = QDir(basePath).absoluteFilePath(result.path);
  if (!QFileInfo::exists(absPath)) {
    qWarning() << "Cross-file link target not found:" << absPath;
    return;
  }

  auto *nbSvc = getServices().get<NotebookCoreService>();
  if (!nbSvc) {
    return;
  }
  QJsonObject resolved = nbSvc->resolvePathToNotebook(absPath);
  if (resolved.isEmpty()) {
    qWarning() << "File not in any notebook:" << absPath;
    WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(absPath));
    return;
  }

  NodeIdentifier nodeId;
  nodeId.notebookId = resolved[QLatin1String(vxcore::kJsonKeyNotebookId)].toString();
  nodeId.relativePath = resolved[QStringLiteral("relativePath")].toString();

  FileOpenSettings settings;
  settings.m_anchor = result.fragment;
  auto *bufSvc = getServices().get<BufferService>();
  if (bufSvc) {
    bufSvc->openBuffer(nodeId, settings);
  }
}

void MarkdownViewWindow2::handleAnchorJump(const QString &p_anchor) {
  if (!m_editor || p_anchor.isEmpty()) {
    return;
  }
  const auto &headings = m_editor->getHeadings();
  for (int i = 0; i < headings.size(); ++i) {
    if (headings[i].m_anchor == p_anchor) {
      m_editor->scrollToHeading(i);
      return;
    }
  }
  qWarning() << "Anchor not found:" << p_anchor;
}

// ============ setMode / setModeInternal ============

void MarkdownViewWindow2::setMode(ViewWindowMode p_mode) { setModeInternal(p_mode, true); }

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
      static_cast<int>(m_previousMode), static_cast<int>(m_mode), m_editor != nullptr,
      m_viewer != nullptr, p_syncBuffer);

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
    if (m_legacyImageBar) {
      m_legacyImageBar->hide();
    }
    // Clear any in-flight message first: a mode change emits no
    // messageVisibilityChanged(true), so an already-visible message would
    // otherwise get stuck hidden with no way to reopen. Then collapse the bar;
    // subsequent transient messages expand it via the messageVisibilityChanged
    // handler wired in setupUI().
    showMessage(QString());
    if (statusBar()) {
      statusBar()->setMaximumHeight(0);
    }
    break;

  case ViewWindowMode::Edit:
    m_editor->show();
    m_editor->setFocus();
    if (m_legacyImageBar) {
      m_legacyImageBar->show();
    }
    if (transition.restoreEditViewMode) {
      setEditViewMode(m_editViewMode);
    } else {
      // First time entering edit: apply config view mode.
      auto *configMgr = getServices().get<ConfigMgr2>();
      const auto &mdConfig = configMgr->getEditorConfig().getMarkdownEditorConfig();
      setEditViewMode(MarkdownViewWindowController::getEditViewMode(mdConfig));
    }
    // Clear any transient message left over from Read mode (e.g. a hovered link
    // URL) so we don't carry it into Edit's editor status surface.
    showMessage(QString());
    if (statusBar()) {
      statusBar()->setMaximumHeight(QWIDGETSIZE_MAX);
    }
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

  // Legacy image-folder check: deferred off this call stack, at most once per
  // window. Detection is itself a cmark parse, so it IS "after the markdown
  // parse"; a highlighter signal would only add latency without removing it.
  if (m_mode == ViewWindowMode::Edit) {
    scheduleLegacyImageCheck();
  }

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

  m_pendingAnchor.clear();

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
  adapter()->scrollToPosition(MarkdownViewerAdapter::Position(m_editor->getTopLine(), QString()));
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
  // In read mode, m_editor is null. Fall back to buffer content.
  return getBuffer().decode(getBuffer().getContentRaw());
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
    if (m_viewer) {
      if (p_zoomIn) {
        m_viewer->zoomIn();
      } else {
        m_viewer->zoomOut();
      }
      // Persistence + status message handled by zoomFactorChanged signal
      // (wired in setupViewer).
    }
    return;
  }
  if (m_editor) {
    m_editor->zoom(m_editor->zoomDelta() + (p_zoomIn ? 1 : -1));
    m_previewHelper->editorZoomChanged();
    int delta = m_editorController->persistZoomDelta(m_editor->zoomDelta());
    showZoomDelta(delta);
  }
}

void MarkdownViewWindow2::resetZoom() {
  if (isReadMode()) {
    if (m_viewer) {
      m_viewer->restoreZoom();
      // Persistence + status message handled by zoomFactorChanged signal.
    }
    return;
  }
  if (m_editor) {
    m_editor->zoom(0);
    m_previewHelper->editorZoomChanged();
    int delta = m_editorController->persistZoomDelta(m_editor->zoomDelta());
    showZoomDelta(delta);
  }
}

QPoint MarkdownViewWindow2::getFloatingWidgetPosition() {
  if (m_editor) {
    return TextViewWindowHelper::getFloatingWidgetPosition(this);
  }
  return ViewWindow2::getFloatingWidgetPosition();
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
      mdConfig, themeService->fetchWebStyleSheet(),
      themeService->getFile(Theme::File::HighlightStyleSheet));

  if (m_editor) {
    auto themeContent = themeService->fetchTextEditorStyle();
    auto syntaxTheme = themeService->getEditorHighlightTheme();
    qreal scaleFactor = WidgetUtils::calculateScaleFactor();

    const auto &widgetConfig = configMgr->getWidgetConfig();
    int maxContentWidth =
        widgetConfig.getViewWindowLayoutMode() == ViewWindowLayoutMode::ReadableWidth
            ? widgetConfig.getReadableWidthMaxPx()
            : 0;

    auto config = MarkdownEditorController::buildMarkdownEditorConfigFromContent(
        editorConfig, mdConfig, themeContent, syntaxTheme, scaleFactor, maxContentWidth);

    // Guard: config application (e.g. applyLineSpacing) modifies block formats,
    // which fires contentsChanged. Suppress propagation so this is not treated
    // as a user edit.
    const bool old = m_propagateEditorToBuffer;
    m_propagateEditorToBuffer = false;
    m_editor->setConfig(config);
    m_editor->updateFromConfig();
    m_propagateEditorToBuffer = old;

    updateEditorFromConfig();
  }

  updateWebViewerConfig();
}

void MarkdownViewWindow2::handleThemeChanged() {
  ViewWindow2::handleThemeChanged(); // base: refreshes toolbar icons

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mdConfig = editorConfig.getMarkdownEditorConfig();
  auto *themeService = getServices().get<ThemeService>();

  // ---- Editor refresh ----
  if (m_editor) {
    auto themeContent = themeService->fetchTextEditorStyle();
    auto syntaxTheme = themeService->getEditorHighlightTheme();
    qreal scaleFactor = WidgetUtils::calculateScaleFactor();

    const auto &widgetConfig = configMgr->getWidgetConfig();
    int maxContentWidth =
        widgetConfig.getViewWindowLayoutMode() == ViewWindowLayoutMode::ReadableWidth
            ? widgetConfig.getReadableWidthMaxPx()
            : 0;

    auto config = MarkdownEditorController::buildMarkdownEditorConfigFromContent(
        editorConfig, mdConfig, themeContent, syntaxTheme, scaleFactor, maxContentWidth);

    // Propagation guard: prevent setConfig from triggering false "modified" state.
    const bool old = m_propagateEditorToBuffer;
    m_propagateEditorToBuffer = false;
    m_editor->setConfig(config);
    m_editor->updateFromConfig();
    m_propagateEditorToBuffer = old;

    updateEditorFromConfig();
  }

  // ---- Viewer refresh ----
  if (m_viewer) {
    // Force-regenerate HTML template with new theme CSS.
    auto *htmlTemplateService = getServices().get<HtmlTemplateService>();
    htmlTemplateService->updateMarkdownViewerTemplate(
        mdConfig, themeService->fetchWebStyleSheet(),
        themeService->getFile(Theme::File::HighlightStyleSheet),
        /*p_force=*/true);

    // Update WebEngine page background.
    m_viewer->page()->setBackgroundColor(themeService->getBaseBackground());

    // Reload viewer with new template (preserves buffer content, does not sync scroll from editor).
    syncViewerFromBuffer(false);

    // Re-apply readable width colors if in readable-width mode.
    applyReadableWidth();
  }

  // Reset external code block highlight styles so they are re-initialized
  // from the new theme's HighlightStyleSheet on next highlight request.
  m_codeBlockStylesInitialized = false;
}

void MarkdownViewWindow2::applyReadableWidth() {
  auto mode = getLayoutMode();
  auto &widgetConfig = getServices().get<ConfigMgr2>()->getWidgetConfig();
  int maxPx = widgetConfig.getReadableWidthMaxPx();

  bool hasEditorOrViewer = false;

  if (m_editor) {
    hasEditorOrViewer = true;
    m_editor->getTextEdit()->setMaxContentWidth(mode == ViewWindowLayoutMode::ReadableWidth ? maxPx
                                                                                            : 0);
  }

  if (m_viewer) {
    hasEditorOrViewer = true;
    if (m_viewerReady) {
      if (mode == ViewWindowLayoutMode::ReadableWidth) {
        m_viewer->page()->runJavaScript(
            QStringLiteral("window.vxcore.setContentMaxWidth(%1)").arg(maxPx));
      } else {
        m_viewer->page()->runJavaScript(QStringLiteral("window.vxcore.setContentMaxWidth(0)"));
      }
    }
  }

  if (!hasEditorOrViewer) {
    ViewWindow2::applyReadableWidth();
  }
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

  // Feed the local-render helpers from the new-architecture config (ConfigMgr2).
  // These are process-wide singletons used by both the in-place preview
  // (PreviewHelper) and the read-mode viewer (MarkdownViewerAdapter). The legacy
  // ViewArea::editorConfigChanged path that used to call update() does not run in
  // the new ViewArea2/MarkdownViewWindow2 UI, and the legacy ConfigMgr singleton
  // they lazy-init from is never loaded from disk, so without this they render
  // with an empty PlantUml JAR / Graphviz path (a broken `java -jar <dir>` command).
  //
  // update() clears the shared render cache, and this method runs on every
  // MarkdownViewWindow2 setup (per note open), so only push into the singletons
  // when the resolved inputs actually change to avoid thrashing the cache and
  // re-spawning java for already-rendered graphs on each note open.
  const auto plantUmlJar = p_mdConfig.getPlantUmlJar();
  const auto graphvizExe = p_mdConfig.getGraphvizExe();
  const auto plantUmlCommand = p_mdConfig.getPlantUmlCommand();
  static QString s_plantUmlJar;
  static QString s_graphvizExe;
  static QString s_plantUmlCommand;
  static bool s_graphHelpersInitialized = false;
  if (!s_graphHelpersInitialized || plantUmlJar != s_plantUmlJar || graphvizExe != s_graphvizExe ||
      plantUmlCommand != s_plantUmlCommand) {
    s_graphHelpersInitialized = true;
    s_plantUmlJar = plantUmlJar;
    s_graphvizExe = graphvizExe;
    s_plantUmlCommand = plantUmlCommand;
    PlantUmlHelper::getInst().update(plantUmlJar, graphvizExe, plantUmlCommand);
    GraphvizHelper::getInst().update(graphvizExe);
  }
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
        disconnect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, m_syncPreviewTimer,
                   QOverload<>::of(&QTimer::start));
      }
      disconnect(m_editor, &MarkdownEditor::topLineChanged, this,
                 &MarkdownViewWindow2::syncEditorPositionToPreview);
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
        m_syncPreviewTimer->setInterval(MarkdownViewWindowController::previewSyncIntervalMs());
        connect(m_syncPreviewTimer, &QTimer::timeout, this,
                &MarkdownViewWindow2::syncEditorContentsToPreview);
      }
      connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, m_syncPreviewTimer,
              QOverload<>::of(&QTimer::start), Qt::UniqueConnection);
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

// ============ Snippet Support ============

void MarkdownViewWindow2::applySnippet(const QString &p_name) {
  if (isReadMode()) {
    showMessage(tr("Snippet insertion is not supported in read mode"));
    return;
  }
  TextViewWindowHelper::applySnippetByName2(this, p_name);
}

void MarkdownViewWindow2::applySnippet() {
  if (isReadMode()) {
    showMessage(tr("Snippet insertion is not supported in read mode"));
    return;
  }
  TextViewWindowHelper::applySnippetPrompt2(this);
}

void MarkdownViewWindow2::clearHighlights() {
  if (isReadMode()) {
    adapter()->findText(QStringList(), FindOption::FindNone);
  } else {
    TextViewWindowHelper::clearSearchHighlights(this);
  }
}

void MarkdownViewWindow2::applyFileOpenSettings(const FileOpenSettings &p_settings) {
  if (p_settings.m_lineNumber < 0 && p_settings.m_anchor.isEmpty() &&
      p_settings.m_cursorOffset < 0 && !p_settings.m_searchHighlight.m_isValid) {
    return;
  }

  // Place the caret at an explicit document position (e.g. a template "@@" mark).
  // Edit mode only — ignored in read mode. Applied after content load; clamped.
  if (p_settings.m_cursorOffset >= 0 && !isReadMode() && m_editor && !m_editor->isReadOnly()) {
    WidgetUtils::applyCursorOffset(m_editor->getTextEdit(), p_settings.m_cursorOffset);
  }

  // Anchor takes precedence over lineNumber when both are set.
  if (!p_settings.m_anchor.isEmpty()) {
    if (isReadMode()) {
      if (m_viewerReady) {
        if (adapter()) {
          adapter()->scrollToPosition(MarkdownViewerAdapter::Position(-1, p_settings.m_anchor));
        }
      } else {
        // Viewer not ready (new file loading) — defer until workFinished.
        m_pendingAnchor = p_settings.m_anchor;
      }
    } else if (m_editor) {
      handleAnchorJump(p_settings.m_anchor);
    }
  } else if (p_settings.m_lineNumber >= 0) {
    // In read mode, skip scrollToPosition when findText will handle scrolling
    // to the current match. The JS findText already calls scrollIntoView() on
    // the matched node, and scrollToPosition defers by 300ms which creates a
    // race condition that overrides the findText scroll.
    const bool searchWillScroll = p_settings.m_searchHighlight.m_isValid &&
                                  p_settings.m_searchHighlight.m_currentMatchLine >= 0;
    if (isReadMode()) {
      if (adapter() && !searchWillScroll) {
        adapter()->scrollToPosition(
            MarkdownViewerAdapter::Position(p_settings.m_lineNumber, QString()));
      }
    } else if (m_editor) {
      m_editor->scrollToLine(p_settings.m_lineNumber, true);
    }
  }

  if (!p_settings.m_searchHighlight.m_isValid) {
    return;
  }

  if (isReadMode()) {
    if (adapter()) {
      adapter()->findText(p_settings.m_searchHighlight.m_patterns,
                          p_settings.m_searchHighlight.m_options,
                          p_settings.m_searchHighlight.m_currentMatchLine);
    }
  } else if (m_editor) {
    const auto result = m_editor->findText(
        p_settings.m_searchHighlight.m_patterns,
        TextViewWindowHelper::toEditorFindFlags(p_settings.m_searchHighlight.m_options), 0, -1,
        p_settings.m_searchHighlight.m_currentMatchLine);
    showFindResult(p_settings.m_searchHighlight.m_patterns, result.m_totalMatches,
                   result.m_currentMatchIndex);
  }
}

// ============ Type Actions ============

void MarkdownViewWindow2::handleTypeAction(int p_action) {
  if (isReadMode() || !m_editor) {
    return;
  }
  switch (p_action) {
  case TypeHeadingNone:
    m_editor->typeHeading(0);
    break;
  case TypeHeading1:
    m_editor->typeHeading(1);
    break;
  case TypeHeading2:
    m_editor->typeHeading(2);
    break;
  case TypeHeading3:
    m_editor->typeHeading(3);
    break;
  case TypeHeading4:
    m_editor->typeHeading(4);
    break;
  case TypeHeading5:
    m_editor->typeHeading(5);
    break;
  case TypeHeading6:
    m_editor->typeHeading(6);
    break;
  case TypeBold:
    m_editor->typeBold();
    break;
  case TypeItalic:
    m_editor->typeItalic();
    break;
  case TypeStrikethrough:
    m_editor->typeStrikethrough();
    break;
  case TypeMark:
    m_editor->typeMark();
    break;
  case TypeUnorderedList:
    m_editor->typeUnorderedList();
    break;
  case TypeOrderedList:
    m_editor->typeOrderedList();
    break;
  case TypeTodoList:
    m_editor->typeTodoList(false);
    break;
  case TypeCheckedTodoList:
    m_editor->typeTodoList(true);
    break;
  case TypeCode:
    m_editor->typeCode();
    break;
  case TypeCodeBlock:
    m_editor->typeCodeBlock();
    break;
  case TypeMath:
    m_editor->typeMath();
    break;
  case TypeMathBlock:
    m_editor->typeMathBlock();
    break;
  case TypeQuote:
    m_editor->typeQuote();
    break;
  case TypeLink:
    m_editor->typeLink();
    break;
  case TypeImage:
    m_editor->typeImage();
    break;
  case TypeTable:
    m_editor->typeTable();
    break;
  default:
    qWarning() << "Unknown type action" << p_action;
    break;
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
  ensureExternalHighlightStyles();
  adapter()->highlightCodeBlock(p_idx, p_timeStamp, p_text);
}

void MarkdownViewWindow2::handleExternalMathHighlightRequest(int p_idx, quint64 p_timeStamp,
                                                             const QString &p_text) {
  ensureExternalHighlightStyles();
  adapter()->highlightMath(p_idx, p_timeStamp, p_text);
}

void MarkdownViewWindow2::ensureExternalHighlightStyles() {
  if (m_codeBlockStylesInitialized) {
    return;
  }
  m_codeBlockStylesInitialized = true;
  auto *themeService = getServices().get<ThemeService>();
  const auto file = themeService->getFile(Theme::File::HighlightStyleSheet);
  if (file.isEmpty()) {
    qWarning() << "no highlight style sheet specified for external code block highlight";
    return;
  }

  QString content;
  Error err = FileUtils2::readTextFile(file, &content);
  if (err) {
    qWarning() << "failed to read highlight style sheet for external code block highlight" << file
               << err.what();
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

bool MarkdownViewWindow2::isReadMode() const { return m_mode == ViewWindowMode::Read; }

void MarkdownViewWindow2::snapshotInitialImages() {
  auto content = getBuffer().decode(getBuffer().getContentRaw());
  auto resolved = getBuffer().resolvedPath();
  if (content.isEmpty() || resolved.isEmpty()) {
    return;
  }

  auto resourcePath = QFileInfo(resolved).path();
  // Remote images must be tracked too: a remote URL still referenced by the
  // content must never be classified obsolete by clearObsoleteImages().
  int linkFlags =
      vte::MarkdownLink::TypeFlag::LocalRelativeInternal | vte::MarkdownLink::TypeFlag::Remote;
  auto images = vte::MarkdownUtils::fetchImagesFromMarkdownText(
      content, resourcePath, static_cast<vte::MarkdownLink::TypeFlags>(linkFlags));
  for (const auto &img : images) {
    if (!img.m_urlInLink.isEmpty()) {
      m_initialImages.insert(img.m_urlInLink);
    }
  }
}

bool MarkdownViewWindow2::isRemoteImageUrl(const QString &p_url) {
  return ImageHostPath::isRemoteUrl(p_url);
}

bool MarkdownViewWindow2::isClearObsoleteImageAtImageHostEnabled() const {
  auto *configMgr = getServices().get<ConfigMgr2>();
  if (!configMgr) {
    return false;
  }
  return configMgr->getEditorConfig().isClearObsoleteImageAtImageHostEnabled();
}

void MarkdownViewWindow2::clearObsoleteImages() {
  auto buffer = getBuffer();
  if (!buffer.isValid()) {
    return;
  }

  const auto content = buffer.decode(buffer.getContentRaw());
  const auto resourcePath = QFileInfo(buffer.resolvedPath()).path();
  // Remote links MUST be collected here. m_insertedImages holds the remote URLs
  // of images uploaded to an image host during this session; if the scan only
  // returned local links, every one of those URLs would be missing from
  // currentImages and therefore treated as obsolete -> deleted at the host on
  // close, even though the note still references it.
  const int linkFlags =
      vte::MarkdownLink::TypeFlag::LocalRelativeInternal | vte::MarkdownLink::TypeFlag::Remote;
  const auto images = vte::MarkdownUtils::fetchImagesFromMarkdownText(
      content, resourcePath, static_cast<vte::MarkdownLink::TypeFlags>(linkFlags));

  QSet<QString> currentImages;
  for (const auto &img : images) {
    if (!img.m_urlInLink.isEmpty()) {
      currentImages.insert(img.m_urlInLink);
    }
  }

  QSet<QString> obsoleteImages = m_initialImages;
  obsoleteImages.unite(m_insertedImages);
  for (auto it = obsoleteImages.begin(); it != obsoleteImages.end();) {
    if (currentImages.contains(*it)) {
      it = obsoleteImages.erase(it);
    } else {
      ++it;
    }
  }

  for (const auto &obsoleteUrl : obsoleteImages) {
    if (obsoleteUrl.isEmpty() || obsoleteUrl.startsWith(QStringLiteral("..")) ||
        QDir::isAbsolutePath(obsoleteUrl) || isRemoteImageUrl(obsoleteUrl)) {
      // A remote URL is not an asset of this notebook. QDir::isAbsolutePath()
      // does not catch it on Windows, so it must be excluded explicitly;
      // otherwise deleteAsset() is handed a bogus "<notebookDir>/https:/..."
      // path and fails.
      continue;
    }

    if (content.contains(obsoleteUrl)) {
      // The Markdown scan misses valid forms (angle-bracket destinations,
      // reference-style links), so a raw-text hit means "still referenced".
      continue;
    }

    // obsoleteUrl is file-parent-relative (from markdown link).
    // deleteAsset expects notebook-root-relative.
    const auto parentPath = buffer.nodeId().parentPath();
    const auto assetRelPath = parentPath.isEmpty()
                                  ? obsoleteUrl
                                  : QDir::cleanPath(parentPath + QStringLiteral("/") + obsoleteUrl);

    const bool deleteOk = buffer.deleteAsset(assetRelPath);
    if (!deleteOk) {
      qWarning() << "MarkdownViewWindow2: failed to delete obsolete image:" << obsoleteUrl;
    }
  }

  // Remote image cleanup via image host controller. This is destructive and
  // IRREVERSIBLE, so both the "Clear obsolete images" setting and the
  // conservative liveness rules are applied by the unit-tested
  // ImageHostPath::remoteUrlsToDelete().
  if (m_imageHostController) {
    const auto toDelete = ImageHostPath::remoteUrlsToDelete(
        isClearObsoleteImageAtImageHostEnabled(), obsoleteImages, currentImages, content);
    for (const auto &imgUrl : toDelete) {
      m_imageHostController->removeAsync(imgUrl);
    }
  }

  m_insertedImages.clear();
  m_initialImages = currentImages;
}

// ============ Legacy image folder migration ============

void MarkdownViewWindow2::scheduleLegacyImageCheck() {
  if (m_legacyImageCheckDone) {
    return;
  }

  // The `this` receiver context makes this lifetime-safe.
  QTimer::singleShot(0, this, &MarkdownViewWindow2::runLegacyImageCheck);
}

void MarkdownViewWindow2::runLegacyImageCheck() {
  // Re-verify the mode FIRST. modeChanged() is synchronous and a slot can flip
  // back to Read before this timer fires; consuming the once-only check in the
  // wrong mode would disable the feature for the window's lifetime.
  if (m_mode != ViewWindowMode::Edit || !m_editor) {
    return;
  }

  if (m_legacyImageCheckDone) {
    return;
  }
  m_legacyImageCheckDone = true;

  auto &buffer = getBuffer();
  if (!buffer.isValid() || buffer.isReadOnly()) {
    return;
  }

  const QString notebookId = buffer.nodeId().notebookId;
  if (notebookId.isEmpty()) {
    // External (non-notebook) file: deleteAsset() has no notebook root to
    // resolve against.
    return;
  }

  const QString assetsFolder = buffer.getAssetsFolder();
  if (assetsFolder.isEmpty()) {
    return;
  }

  auto *notebookSvc = getServices().get<NotebookCoreService>();
  if (!notebookSvc) {
    return;
  }
  const QString notebookRoot = notebookSvc->buildAbsolutePath(notebookId, QString());
  if (notebookRoot.isEmpty() ||
      !LegacyImageMigrationController::isPathContained(notebookRoot, assetsFolder)) {
    // An assets folder configured outside the notebook root is unsupported:
    // deleteAsset() is notebook-root relative. The check is CANONICAL, so an
    // in-notebook junction pointing outside does not slip through. No bar is
    // better than a bar that half-delivers.
    return;
  }

  if (!m_legacyImageController) {
    m_legacyImageController = new LegacyImageMigrationController(getServices(), this);
  }
  if (m_legacyImageController->isOptedOut(notebookId)) {
    return;
  }

  const QString resolved = buffer.resolvedPath();
  if (resolved.isEmpty()) {
    return;
  }

  // Same basePath convention as snapshotInitialImages().
  const auto refs = LegacyImageMigrationController::detect(
      m_editor->getTextEdit()->document()->toPlainText(), QFileInfo(resolved).path(), assetsFolder);
  if (refs.isEmpty()) {
    return;
  }

  m_legacyImageBar = new LegacyImageMigrationBar(this);
  m_legacyImageBar->setImageCount(refs.size());
  // addToolBar() has already run in setupUI(), which addTopWidget() requires.
  addTopWidget(m_legacyImageBar);

  connect(m_legacyImageBar, &LegacyImageMigrationBar::migrateRequested, this,
          &MarkdownViewWindow2::applyLegacyImageMigration);
  connect(m_legacyImageBar, &LegacyImageMigrationBar::dismissRequested, this, [this]() {
    if (m_legacyImageBar) {
      m_legacyImageBar->deleteLater();
      m_legacyImageBar = nullptr;
    }
  });
  connect(m_legacyImageBar, &LegacyImageMigrationBar::neverRequested, this, [this]() {
    const QString nbId = getBuffer().nodeId().notebookId;
    if (m_legacyImageController && !m_legacyImageController->setOptedOut(nbId)) {
      showMessage(tr("Failed to save the preference."));
    }
    if (m_legacyImageBar) {
      m_legacyImageBar->deleteLater();
      m_legacyImageBar = nullptr;
    }
  });
}

void MarkdownViewWindow2::applyLegacyImageMigration() {
  if (!m_editor || !m_legacyImageController) {
    return;
  }

  auto &buffer = getBuffer();
  if (!buffer.isValid()) {
    return;
  }

  const QString resolved = buffer.resolvedPath();
  const QString assetsFolder = buffer.getAssetsFolder();
  if (resolved.isEmpty() || assetsFolder.isEmpty()) {
    return;
  }

  auto *doc = m_editor->getTextEdit()->document();

  // Re-run detection: the user may have typed since the bar appeared.
  const auto refs = LegacyImageMigrationController::detect(
      doc->toPlainText(), QFileInfo(resolved).path(), assetsFolder);
  if (refs.isEmpty()) {
    if (m_legacyImageBar) {
      m_legacyImageBar->deleteLater();
      m_legacyImageBar = nullptr;
    }
    return;
  }

  QString err;
  const auto rewrites = LegacyImageMigrationController::stageAssets(
      refs, [this](const QString &p_src) { return getBuffer().insertAsset(p_src); }, assetsFolder,
      [this](const QString &p_abs) { return m_editor->getRelativeLink(p_abs); }, &err);
  if (rewrites.isEmpty()) {
    // All-or-nothing: nothing was copied and nothing was rewritten. Keep the
    // bar so the user can retry.
    MessageBoxHelper::notify(MessageBoxHelper::Warning,
                             err.isEmpty() ? tr("Failed to migrate the images.") : err, this);
    return;
  }

  // One undoable edit. The rewrites are already sorted DESCENDING by
  // urlInLinkPos, which keeps every earlier offset valid — never sort ascending.
  {
    QTextCursor cur(doc);
    cur.beginEditBlock();
    for (const auto &rw : rewrites) {
      cur.setPosition(rw.urlInLinkPos);
      cur.setPosition(rw.urlInLinkPos + rw.oldUrlInLink.size(), QTextCursor::KeepAnchor);
      cur.insertText(rw.newUrlInLink);
    }
    cur.endEditBlock();
  }

  // Push the rewritten text into the vxcore buffer so the in-memory state is
  // coherent immediately. Not fatal on failure — the close-time gate reads the
  // file on DISK and is the real authority.
  auto *bufferSvc = getServices().get<BufferService>();
  if (bufferSvc) {
    if (!buffer.setContentRaw(bufferSvc->encodeContent(buffer.id(), doc->toPlainText()))) {
      qWarning() << "MarkdownViewWindow2: failed to push migrated content into the buffer";
    }
  }

  // Keep the image-tracking sets in step so clearObsoleteImages() can never
  // race the finalize: afterwards it can only conclude that the STAGED COPIES
  // are obsolete (the undo case), never the originals.
  QSet<QString> distinctDestinations;
  for (const auto &rw : rewrites) {
    m_initialImages.remove(rw.oldUrlInLink);
    m_initialImages.insert(rw.newUrlInLink);
    m_insertedImages.insert(rw.newUrlInLink);
    if (!rw.destAbsolutePath.isEmpty()) {
      distinctDestinations.insert(rw.destAbsolutePath);
    }
  }

  m_pendingLegacyMigration = rewrites;

  // Best-effort, plain Ctrl+S semantics. The return value is NOT a correctness
  // gate — see LegacyImageMigrationController::finalize().
  save();

  if (m_legacyImageBar) {
    m_legacyImageBar->deleteLater();
    m_legacyImageBar = nullptr;
  }

  showMessage(tr("Migrated %n image(s) to the assets folder.", "", distinctDestinations.size()));
}

bool MarkdownViewWindow2::isLastWindowForBuffer() const {
  auto *wsSvc = getServices().get<WorkspaceCoreService>();
  if (!wsSvc || !getBuffer().isValid()) {
    return true;
  }

  const auto bufferId = getBuffer().id();
  if (bufferId.isEmpty()) {
    return true;
  }

  int count = 0;
  const auto workspaces = wsSvc->listWorkspaces();
  for (const auto &workspaceValue : workspaces) {
    const auto bufferIds = workspaceValue.toObject().value(QStringLiteral("bufferIds")).toArray();
    for (const auto &bufferIdValue : bufferIds) {
      if (bufferIdValue.toString() == bufferId) {
        ++count;
        if (count > 1) {
          return false;
        }
      }
    }
  }

  return true;
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

ViewWindow2::ViewScrollState MarkdownViewWindow2::captureScrollState() const {
  ViewScrollState s;
  if (m_mode == ViewWindowMode::Edit && m_editor) {
    if (auto *vbar = m_editor->getTextEdit()->verticalScrollBar()) {
      s.m_scrollValue = vbar->value();
      s.m_scrollMax = vbar->maximum();
    }
  } else if (m_mode == ViewWindowMode::Read && m_viewer) {
    if (auto *a = adapter()) {
      s.m_topLineNumber = a->getTopLineNumber();
    }
  }
  return s;
}

void MarkdownViewWindow2::restoreScrollState(const ViewScrollState &p_state) {
  if (m_mode == ViewWindowMode::Edit && m_editor && p_state.m_scrollValue >= 0) {
    if (auto *vbar = m_editor->getTextEdit()->verticalScrollBar()) {
      const int target = ScrollPreservationPolicy::computeRestoredScrollValue(
          p_state.m_scrollValue, p_state.m_scrollMax, vbar->maximum());
      vbar->setValue(target);
    }
  } else if (m_mode == ViewWindowMode::Read && m_viewer && p_state.m_topLineNumber >= 0) {
    if (auto *a = adapter()) {
      const int line =
          ScrollPreservationPolicy::computeRestoredReadModeLine(p_state.m_topLineNumber);
      if (line >= 0) {
        // Use the PUBLIC scrollToPosition API. The QWebChannel + JS bridge
        // handle async timing internally. Do NOT call the private
        // scrollToLine(int) at adapter.h:224.
        a->scrollToPosition(MarkdownViewerAdapter::Position(line, QString()));
      }
    }
  }
}

void MarkdownViewWindow2::updateImageHostMenu() {
  if (!m_imageHostMenu) {
    return;
  }
  m_imageHostMenu->clear();

  auto *actionGroup = new QActionGroup(m_imageHostMenu);

  // "Local" option (no image host).
  auto *localAct = actionGroup->addAction(tr("Local"));
  localAct->setCheckable(true);
  localAct->setData(QString());
  m_imageHostMenu->addAction(localAct);

  if (m_imageHostController) {
    auto providers = m_imageHostController->getProviders();
    auto *defaultProvider = m_imageHostController->getDefaultProvider();
    for (auto *provider : providers) {
      auto *act = actionGroup->addAction(provider->getName());
      act->setCheckable(true);
      act->setData(provider->getName());
      m_imageHostMenu->addAction(act);
      if (provider == defaultProvider) {
        act->setChecked(true);
      }
    }
    if (!defaultProvider) {
      localAct->setChecked(true);
    }
  } else {
    localAct->setChecked(true);
  }
}

void MarkdownViewWindow2::handleImageHostChanged(const QString &p_providerName) {
  if (!m_imageHostController) {
    return;
  }
  if (p_providerName.isEmpty()) {
    // "Local" selected — clear image host on editor.
    m_editor->setImageHostController(nullptr);
  } else {
    m_imageHostController->setDefaultProvider(p_providerName);
    m_editor->setImageHostController(m_imageHostController);
  }
}
