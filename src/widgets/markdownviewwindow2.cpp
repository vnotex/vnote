#include "markdownviewwindow2.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLayout>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPrinter>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QWebEngineLoadingInfo>
#include <QWebEngineProfile>
#include <QWindow>

#include <algorithm>
#include <stdexcept>

#include <vtextedit/markdowndocument.h>
#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/markdownhighlighter.h>
#include <vtextedit/markdownutils.h>
#include <vtextedit/previewmgr.h>
#include <vtextedit/texteditutils.h>
#include <vtextedit/vtextedit.h>

#include <controllers/imagehostcontroller.h>
#include <controllers/markdowneditorcontroller.h>
#include <controllers/markdownrendercontroller.h>
#include <controllers/markdownviewwindowcontroller.h>
#include <controllers/markdownwebjobcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/logging.h>
#include <core/markdowneditorconfig.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/htmltemplateservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/workspacecoreservice.h>
#include <core/sessionconfig.h>
#include <core/theme.h>
#include <core/widgetconfig.h>
#include <gui/services/themeservice.h>
#include <gui/services/webengineprofileservice.h>
#include <gui/services/webviewjobs.h>
#include <gui/utils/externallinkconfirmation.h>
#include <gui/utils/printutils.h>
#include <gui/utils/widgetutils.h>
#include <imagehost/iimagehostprovider.h>
#include <imagehost/imagehostpath.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>
#include <utils/sectionnumberutils.h>
#include <utils/urlutils.h>
#include <vxcore/notebook_json_keys.h>

#include "../utils/scrollpreservationpolicy.h"
#include "editors/editorstatusbarbinder.h"
#include "editors/graphvizhelper.h"
#include "editors/markdowneditor.h"
#include "editors/markdownviewer.h"
#include "editors/markdownvieweradapter.h"
#include "editors/markdownwebjobhost.h"
#include "editors/plantumlhelper.h"
#include "editors/previewhelper.h"
#include "editors/statusbar.h"
#include "encodingbutton.h"
#include "legacyimagemigrationbar.h"
#include "messageboxhelper.h"
#include "outlinepopup.h"
#include "outlineprovider.h"
#include "presentationtoolbareffect.h"
#include "propertydefs.h"
#include "tableinsertpopup.h"
#include "textviewwindowhelper.h"
#include "viewwindowtoolbarhelper2.h"
#include "webpage.h"
#include "webviewer.h"

using namespace vnotex;

struct MarkdownViewWindow2::ProtectedView {
  WebEngineProfileService::ProtectedPage page;
  QByteArray highlightStyleSheet;
  bool documentPublished = false;
  QList<int> splitterSizesOnReady;
};

// ============ Constructor ============

MarkdownViewWindow2::MarkdownViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                         QWidget *p_parent, ViewWindowMode p_initialMode)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  // Start at Invalid so the first setMode() triggers full initialization.
  m_mode = ViewWindowMode::Invalid;
  m_inactiveReaderTimer = new QTimer(this);
  m_inactiveReaderTimer->setObjectName(QStringLiteral("inactiveReaderTimer"));
  m_inactiveReaderTimer->setSingleShot(true);
  connect(m_inactiveReaderTimer, &QTimer::timeout, this,
          &MarkdownViewWindow2::unloadInactiveReader);
  m_editorController = new MarkdownEditorController(p_services, this);
  m_windowController = new MarkdownViewWindowController(p_services, this);
  m_imageHostController = p_services.get<ImageHostController>();
  m_webJobs = new MarkdownWebJobController(p_services, getBuffer(), this);
  m_nativeRender = new MarkdownRenderController(p_services, getBuffer(), this);
  connect(m_nativeRender, &MarkdownRenderController::rendered, this,
          [this](const QString &p_token, const MarkdownHtmlResult &p_result) {
            if (!m_viewer || m_readerResourcesSuspended || m_readerLifecycleClosing ||
                m_nativeLocking || p_token != m_nativeRenderToken ||
                !m_nativeRender->isCurrent(p_token) || !nativeRenderSourceIsCurrent()) {
              return;
            }
            if (!p_result.error.isEmpty()) {
              showMessage(tr("Unable to render Markdown"));
              if (m_printPending) {
                m_printPending = false;
                m_printer.reset();
              }
              return;
            }
            adapter()->setRenderedHtml(p_token, p_result.html, m_nativeRenderLineNumber);
          });
  if (auto *buffers = p_services.get<BufferService>()) {
    m_nativeLocking = getBuffer().isEncrypted() && buffers->isProtectedLocking();
    connect(buffers->asQObject(), SIGNAL(protectedLockingChanged(bool)), this,
            SLOT(onNativeProtectedLockingChanged(bool)), Qt::DirectConnection);
  }
  m_webJobHost = new MarkdownWebJobHost(p_services, getBuffer(), this);
  connect(m_webJobs, &MarkdownWebJobController::protectedSessionRequested, m_webJobHost,
          &MarkdownWebJobHost::ensureProtectedSession);
  connect(m_webJobHost, &MarkdownWebJobHost::protectedSessionReady, m_webJobs,
          &MarkdownWebJobController::setProtectedSession);
  connect(m_webJobs, &MarkdownWebJobController::protectedSessionReleaseRequested, m_webJobHost,
          &MarkdownWebJobHost::releaseProtectedSession);
  connect(m_webJobs, &MarkdownWebJobController::ready, this, [this]() {
    // Readiness identifies a newly usable endpoint/generation. Its math scope has
    // no earlier declarations, so cached rasters must not skip ordered source replay.
    if (m_previewHelper) {
      m_previewHelper->invalidatePreviews();
    }
    refreshEditorWebJobs();
  });
  connect(m_webJobs, &MarkdownWebJobController::operationFailed, this,
          [this](const QString &p_operation, quint64, quint64, const QString &) {
            if (p_operation == QStringLiteral("markdown.htmlToMarkdown")) {
              showMessage(tr("Unable to convert HTML to Markdown"));
            }
          });

  setupOutlineProvider();
  setupUI();
  setupPreviewHelper();

  // Trigger initial mode setup directly to the requested mode.
  // Going straight to Edit (Invalid -> Edit) avoids a visible Read -> Edit
  // transition (flash of blank viewer in QSplitter).
  setModeInternal(p_initialMode, true);

  setupShortcuts();

  snapshotInitialImages();
  QTimer::singleShot(0, this, [this]() { updateInactiveReaderTimer(); });
}

// ============ Destructor ============

MarkdownViewWindow2::~MarkdownViewWindow2() {
  m_readerLifecycleClosing = true;
  m_inactiveReaderTimer->stop();
  invalidateNativeRender(true);
  m_nativeRender->revoke();
  cancelEditorImageView();
  setPresentationMode(false);
  if (m_viewer && m_debugViewer) {
    m_viewer->page()->setDevToolsPage(nullptr);
  }
  delete m_debugViewer;
  m_debugViewer = nullptr;
  m_webJobs->revoke();
  m_webJobHost->releaseProtectedSession();
  releaseProtectedView();
  // Disconnect controller signals to prevent delivery to a destroyed widget.
  if (m_imageHostController) {
    disconnect(m_imageHostController, nullptr, this, nullptr);
  }

  // The StatusBar is a QObject child of the window and the binder is parented to
  // this; both are cleaned up automatically. The encoding button is owned by the
  // bar's Widget-column mount (raw overload).
}

void MarkdownViewWindow2::releaseProtectedView() {
  cancelEditorImageView();
  setPresentationMode(false);
  ++m_navigationGeneration;
  if (m_viewer) {
    m_viewer->invalidateNavigationTargets();
  }
  if (!getBuffer().isEncrypted()) {
    return;
  }
  m_readerLifecycleClosing = true;
  m_readerResourcesSuspended = true;
  m_inactiveReaderTimer->stop();
  m_inactiveReaderFileOpenSettings.reset();
  m_restoreReaderStatePending = false;
  m_webJobs->revoke();
  invalidateNativeRender(true);
  m_nativeRender->revoke();
  m_webJobHost->releaseProtectedSession();
  ++m_highlightStyleGeneration;
  m_codeBlockStylesInitialized = false;
  m_codeBlockStylesPending = false;
  if (m_syncPreviewTimer) {
    m_syncPreviewTimer->stop();
  }
  m_viewerReady = false;
  m_pendingAnchor.clear();
  setEnabled(false);
  // Retire cached graph source/results and native resources before the page.
  // The off-the-record profile must outlive every page that used it.
  if (m_editor) {
    m_editor->revokeProtectedResources();
    disconnect(m_editor, nullptr, this, nullptr);
  }
  delete m_previewHelper;
  m_previewHelper = nullptr;
  setStatusBarDef(StatusBarDef());
  delete m_statusBinder;
  m_statusBinder = nullptr;
  delete m_editor;
  m_editor = nullptr;
  m_outlineProvider->setOutline(QSharedPointer<Outline>());
  retireReader();
}

void MarkdownViewWindow2::retireReader() {
  setDebugVisible(false);
  delete m_debugViewer;
  m_debugViewer = nullptr;
  m_webJobs->setPreferredSession(nullptr);
  if (m_protectedView) {
    getServices().get<WebEngineProfileService>()->revokeProtectedProfile(
        m_protectedView->page.profile);
  }
  m_viewerReady = false;
  m_viewerPageInitialized = false;
  auto *oldViewer = m_viewer;
  m_viewer = nullptr;
  // The immutable authorization profile must outlive its page and channel.
  delete oldViewer;
  if (m_protectedView) {
    delete m_protectedView->page.profile;
    m_protectedView.reset();
  }
  m_printer.reset();
}

void MarkdownViewWindow2::releaseBufferResources() {
  m_rebindingBuffer = true;
  invalidateNativeRender(true);
  m_readerResourcesSuspended = true;
  m_switchingMode = false;
  m_propagateEditorToBuffer = false;
  cancelEditorImageView();
  setPresentationMode(false);
  setDebugVisible(false);
  updateEditSectionNumberOptions(false);
  m_inactiveReaderTimer->stop();
  m_inactiveReaderFileOpenSettings.reset();
  m_restoreReaderStatePending = false;
  m_pendingAnchor.clear();
  m_deferredPreviewUpdate = false;
  ++m_navigationGeneration;
  if (m_viewer) {
    m_inactiveReaderState.m_zoomFactor = m_viewer->zoomFactor();
    m_inactiveReaderState.m_splitterSizes = m_splitter->sizes();
    m_viewer->invalidateNavigationTargets();
    m_viewer->jobSession()->invalidate();
    disconnect(m_viewer, nullptr, this, nullptr);
    disconnect(m_viewer->page(), nullptr, this, nullptr);
    disconnect(adapter(), nullptr, this, nullptr);
    m_viewer->stop();
    adapter()->reset();
    m_viewer->hide();
  }
  m_inactiveReaderState.m_topLineNumber = -1;
  m_viewerReady = false;
  m_viewerPageInitialized = false;
  if (m_printer) {
    m_printer->abort();
  }
  invalidateWebJobs(true);
  if (m_editor) {
    m_editor->setInplacePreviewEnabled(false);
    m_editor->revokeProtectedResources();
    // Ordinary insertion/upload continuations also belong to the outgoing document.
    // Keep the protected marker until bind so setBuffer2 restores an ordinary reader.
    if (!getBuffer().isEncrypted()) {
      m_editor->setBuffer2(nullptr);
      m_editor->getPreviewMgr()->setResourceReader({}, true);
    }
    m_editor->setImageHostController(nullptr);
    m_editor->setReadOnly(true);
    m_editor->hide();
  }
  for (auto *dialog : findChildren<QDialog *>()) {
    dialog->reject();
  }
  for (auto *menu : findChildren<QMenu *>()) {
    menu->close();
  }
  finalizeBufferImages();
  if (getBuffer().isEncrypted()) {
    retireReader();
  }
  delete m_legacyImageBar;
  m_legacyImageBar = nullptr;
  m_legacyImageCheckDone = false;
  m_pendingLegacyMigration.clear();
  m_initialImages.clear();
  m_insertedImages.clear();
  m_outlineProvider->setOutline(QSharedPointer<Outline>());
  m_outlineProvider->setCurrentHeadingIndex(-1);
  m_outlineProvider->setReorderSupported(false);
  m_textEditorBufferRevision = -1;
  m_viewerBufferRevision = -1;
  m_previousMode = ViewWindowMode::Invalid;
  m_sourceAvailable = false;
}

void MarkdownViewWindow2::bindBufferResources() {
  // Even an ordinary outgoing page cannot be retargeted to a protected identity.
  if (getBuffer().isEncrypted() && m_viewer) {
    retireReader();
  }
  m_previewHelper->setProtectedView(getBuffer().isEncrypted());
  if (m_editor) {
    m_editor->setBuffer2(&getBuffer());
    if (!getBuffer().isEncrypted()) {
      m_editor->getPreviewMgr()->setResourceReader({}, false);
    }
    m_editor->setImageHostController(getBuffer().isEncrypted() ? nullptr : m_imageHostController);
  }
  m_resetTextEditorForNewBuffer = true;
  const auto *buffers = getServices().get<BufferService>();
  m_nativeLocking = getBuffer().isEncrypted() && buffers && buffers->isProtectedLocking();
  updateNativeDocumentRetention();
  m_readerResourcesSuspended = false;
  if (m_viewer) {
    connectViewerSignals();
  }
  // Native and web jobs remain suspended until source, reader identity and mode are installed.
}

// ============ setupUI ============

void MarkdownViewWindow2::setupUI() {
  installEventFilter(this);
  // Keep the inspector outside the editor/preview splitter.
  m_mainSplitter = new QSplitter(Qt::Vertical, this);
  m_mainSplitter->setContentsMargins(0, 0, 0, 0);
  m_mainSplitter->setChildrenCollapsible(false);

  // Inner splitter holds editor (index 0) and viewer (index 1).
  m_splitter = new QSplitter(m_mainSplitter);
  m_splitter->setContentsMargins(0, 0, 0, 0);
  m_mainSplitter->addWidget(m_splitter);
  m_mainSplitter->setFocusProxy(m_splitter);
  setCentralWidget(m_mainSplitter);
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

    // Read-mode auto-hide: only show the floating bar for transient messages.
    if (statusBar()) {
      connect(statusBar(), &StatusBar::messageVisibilityChanged, this, [this](bool p_visible) {
        if (m_mode == ViewWindowMode::Read && statusBar()) {
          statusBar()->setVisible(p_visible);
        }
      });
    }
  }

  setupToolBar();
}

void MarkdownViewWindow2::setDebugVisible(bool p_visible) {
  p_visible = p_visible && isReadMode() && m_viewer && !getBuffer().isEncrypted();
  if (m_debugAction) {
    const QSignalBlocker blocker(m_debugAction);
    m_debugAction->setChecked(p_visible);
  }

  if (!p_visible) {
    auto *focused = QApplication::focusWidget();
    const bool restoreFocus = m_debugViewer && focused &&
                              (focused == m_debugViewer || m_debugViewer->isAncestorOf(focused));
    if (m_viewer) {
      m_viewer->page()->setDevToolsPage(nullptr);
    }
    if (m_debugViewer) {
      m_debugViewer->hide();
    }
    if (restoreFocus && m_viewer && m_viewer->isVisible()) {
      m_viewer->setFocus();
    }
    return;
  }

  const bool firstOpen = (m_debugViewer == nullptr);
  if (firstOpen) {
    m_debugViewer =
        new WebViewer(getServices().get<ThemeService>()->getBaseBackground(), m_mainSplitter);
    m_mainSplitter->addWidget(m_debugViewer);
    connect(m_debugViewer->page(), &QWebEnginePage::windowCloseRequested, this,
            [this]() { setDebugVisible(false); });
  }
  m_debugViewer->show();
  m_viewer->page()->setDevToolsPage(m_debugViewer->page());
  if (firstOpen) {
    WidgetUtils::distributeWidgetsOfSplitter(m_mainSplitter);
  }
}

// ============ setupToolBar ============

void MarkdownViewWindow2::setupToolBar() {
  auto *toolBar = createToolBar(this);
  addToolBar(toolBar);

  addAction(toolBar, ViewWindowToolBarHelper2::EditRead);
  addLeftCommonToolBarActions(toolBar);
  const auto [undo, redo] = TextViewWindowHelper::addUndoRedoActions(toolBar, getServices(), this);
  m_undoAction = undo;
  m_redoAction = redo;

  // Separator between history and formatting actions (visible only in edit mode).
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
  auto *tableAction = addAction(toolBar, ViewWindowToolBarHelper2::TypeTable);
  auto *tableButton = qobject_cast<QToolButton *>(toolBar->widgetForAction(tableAction));
  if (tableButton) {
    tableButton->setPopupMode(QToolButton::InstantPopup);
    tableButton->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);
    auto *tablePopup = new TableInsertPopup(tableButton, toolBar);
    tableAction->setMenu(tablePopup);
    connect(tablePopup, &TableInsertPopup::tableSelected, this,
            [this](int p_bodyRows, int p_columns) {
              if (m_editor) {
                m_editor->typeTable(p_bodyRows, p_columns);
              }
            });
    connect(tablePopup, &TableInsertPopup::dialogRequested, this,
            [this]() { handleTypeAction(TypeTable); });
  }

  addRightCommonToolBarActions(toolBar);
}

void MarkdownViewWindow2::addAdditionalRightToolBarActions(QToolBar *p_toolBar) {
  // Keep Outline directly on the toolbar.
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
}

void MarkdownViewWindow2::addAdditionalViewToolBarActions(QToolBar *p_toolBar) {
  const auto iconName = QStringLiteral("presentation_editor.svg");
  m_presentationAction = p_toolBar->addAction(
      ViewWindowToolBarHelper2::generateIcon(getServices(), iconName), tr("Presentation Mode"));
  m_presentationAction->setProperty("iconName", iconName);
  ViewWindowToolBarHelper2::addActionShortcut(
      m_presentationAction,
      getServices().get<ConfigMgr2>()->getEditorConfig().getShortcut(
          EditorConfig::Shortcut::PresentationMode),
      this);
  m_presentationAction->setCheckable(true);
  m_presentationEffect = new PresentationToolBarEffect(p_toolBar, this);

  connect(m_presentationAction, &QAction::triggered, this, [this]() {
    // QAction toggles before triggered(), but only the confirmed deck may be checked.
    {
      const QSignalBlocker blocker(m_presentationAction);
      m_presentationAction->setChecked(m_presentationActive && isViewFullScreen());
    }
    setPresentationMode(!m_presentationRequested);
  });
  connect(this, &ViewWindow2::viewFullScreenExitRequested, this,
          [this]() { setPresentationMode(false); });
  connect(this, &ViewWindow2::viewFullScreenChanged, this, [this](bool p_on) {
    if (!p_on && (m_presentationRequested || m_presentationActive)) {
      setPresentationMode(false);
    }
    if (p_on) {
      m_presentationBackground = QColor(
          getServices().get<ThemeService>()->paletteColor(QStringLiteral("base#content#bg")));
      const QPointer<MarkdownViewWindow2> self(this);
      const QPointer<MarkdownViewer> viewer(m_viewer);
      const auto generation = m_presentationGeneration;
      QTimer::singleShot(0, this, [self, viewer, generation]() {
        const auto current = [self, viewer, generation]() {
          return self && viewer && self->m_viewer == viewer.data() &&
                 self->m_presentationGeneration == generation && self->m_presentationRequested &&
                 self->m_presentationActive && self->m_viewerReady && self->isViewFullScreen();
        };
        if (!current()) {
          return;
        }
        if (self->layout()) {
          self->layout()->activate();
        }
        if (current()) {
          viewer->page()->runJavaScript(
              QStringLiteral("if (window.vxPresentation) { window.vxPresentation.layout(); }"));
        }
      });
    }
    updatePresentationUi();
  });
}

void MarkdownViewWindow2::setPresentationMode(bool p_on) {
  if (!p_on) {
    if (!m_presentationRequested && !m_presentationRequestSent && !m_presentationActive &&
        !isViewFullScreen()) {
      return;
    }
    const bool cancelWeb = m_presentationRequestSent;
    m_presentationRequested = false;
    m_presentationRequestSent = false;
    m_presentationActive = false;
    ++m_presentationGeneration;
    if (!m_editorImageRequest) {
      qApp->removeEventFilter(this);
    }
    if (cancelWeb && adapter() && adapter()->isReady()) {
      adapter()->setPresentationMode(false, false);
    }
    setViewFullScreen(false);
    updatePresentationUi();
    return;
  }

  if (m_presentationRequested || m_switchingMode || !isEnabled() || !isVisible()) {
    return;
  }
  m_presentationRequested = true;
  ++m_presentationGeneration;
  // The fullscreen host owns Escape once promoted; cover the pending page/deck too.
  qApp->installEventFilter(this);
  const QPointer<MarkdownViewWindow2> guard(this);
  m_preparingPresentation = true;
  // The normal Edit -> Read path syncs unsaved source through BufferService::syncNow().
  setMode(ViewWindowMode::Read);
  if (!guard) {
    return;
  }
  m_preparingPresentation = false;
  if (!isVisible() || !isEnabled()) {
    setPresentationMode(false);
    return;
  }
  if (!m_presentationRequested) {
    return;
  }
  if (!isReadMode() || !m_viewer) {
    setPresentationMode(false);
    showMessage(tr("Unable to start presentation"));
    return;
  }
  setDebugVisible(false);
  requestPresentationWhenReady();
}

void MarkdownViewWindow2::requestPresentationWhenReady() {
  if (!m_presentationRequested || m_presentationRequestSent || m_preparingPresentation ||
      m_switchingMode || !isReadMode() || !m_viewerReady || !adapter() ||
      !adapter()->isDocumentReady() ||
      !m_nativeRender->isCurrent(adapter()->documentGeneration()) ||
      !nativeRenderSourceIsCurrent()) {
    return;
  }
  m_presentationRequestSent = true;
  const auto *theme = getServices().get<ThemeService>();
  QColor background(theme->optionalPaletteColor(
      QStringLiteral("base#content#bg"))); // palette-token-optional: custom themes may omit this
  if (!background.isValid()) {
    background = theme->getBaseBackground();
  }
  adapter()->setPresentationMode(true, qGray(background.rgb()) < 128);
}

void MarkdownViewWindow2::handlePresentationStateChanged(bool p_active, const QString &p_error) {
  if (!p_error.isEmpty()) {
    qWarning() << "Markdown presentation failed:" << p_error;
  }
  if (!p_active || !p_error.isEmpty()) {
    // Rendering the initial page may report inactive before our request was dispatched.
    if (!m_presentationRequestSent && !m_presentationActive) {
      return;
    }
    const bool showError = m_presentationRequested && !p_error.isEmpty();
    setPresentationMode(false);
    if (showError) {
      showMessage(tr("Unable to start presentation"));
    }
    return;
  }

  if (!m_presentationRequested || !m_presentationRequestSent || !isReadMode() || !m_viewerReady ||
      !isEnabled() || !isVisible()) {
    setPresentationMode(false);
    // A ready callback can arrive after cancellation. Never resurrect the native window.
    if (adapter() && adapter()->isReady()) {
      adapter()->setPresentationMode(false, false);
    }
    return;
  }

  m_presentationActive = true;
  qApp->removeEventFilter(this);
  if (!isViewFullScreen() && !setViewFullScreen(true)) {
    handlePresentationStateChanged(false, QStringLiteral("Unable to enter native fullscreen"));
    return;
  }
  // Focus/layout events during promotion can cancel the request synchronously.
  if (!m_presentationRequested || !m_presentationActive) {
    setPresentationMode(false);
    return;
  }
  updatePresentationUi();
}

void MarkdownViewWindow2::updatePresentationUi() {
  const bool active = m_presentationActive && isViewFullScreen();
  if (m_presentationAction) {
    const QSignalBlocker blocker(m_presentationAction);
    m_presentationAction->setChecked(active);
  }
  if (m_presentationEffect) {
    m_presentationEffect->setActive(active);
  }
  update();
}

void MarkdownViewWindow2::paintEvent(QPaintEvent *p_event) {
  ViewWindow2::paintEvent(p_event);
  if (isViewFullScreen()) {
    QPainter painter(this);
    painter.fillRect(rect(), m_presentationBackground);
  }
}

void MarkdownViewWindow2::addAdditionalMenuActions(QMenu *p_menu) {
  p_menu->addSeparator();

  // Image hosts are not an available storage destination for protected notes.
  {
    auto *act = addAction(p_menu, ViewWindowToolBarHelper2::ImageHost);
    m_imageHostMenu = act->menu();
    updateImageHostMenu();
    connect(m_imageHostMenu, &QMenu::triggered, this,
            [this](QAction *p_act) { handleImageHostChanged(p_act->data().toString()); });
    if (m_imageHostController) {
      connect(m_imageHostController, &ImageHostController::providerChanged, this,
              &MarkdownViewWindow2::updateImageHostMenu);
    }

    // Image host button is only relevant while editing.
    act->setVisible(m_mode == ViewWindowMode::Edit && !getBuffer().isEncrypted());
    connect(this, &ViewWindow2::modeChanged, this, [act, this]() {
      act->setVisible(m_mode == ViewWindowMode::Edit && !getBuffer().isEncrypted());
    });
  }

  // Live preview toggle (visible only in Edit mode).
  {
    auto *livePreviewAction = addAction(p_menu, ViewWindowToolBarHelper2::ToggleLivePreview);
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
    auto *inplacePreviewAction = addAction(p_menu, ViewWindowToolBarHelper2::InplacePreview);
    // setChecked before connect so this does not fire the toggled handler.
    inplacePreviewAction->setChecked(m_inplacePreviewEnabled);
    connect(inplacePreviewAction, &QAction::toggled, this, [this](bool p_checked) {
      m_inplacePreviewEnabled = p_checked;
      if (m_mode == ViewWindowMode::Edit && m_editor) {
        m_editor->setInplacePreviewEnabled(p_checked);
      }
    });
  }

  {
    auto *destinations = addAction(p_menu, ViewWindowToolBarHelper2::ConcealDestinations);
    auto *markers = addAction(p_menu, ViewWindowToolBarHelper2::ConcealMarkdownMarkers);
    const auto refreshConcealmentActions = [this, destinations, markers]() {
      const auto &config =
          getServices().get<ConfigMgr2>()->getEditorConfig().getMarkdownEditorConfig();
      const QSignalBlocker destinationsBlocker(destinations);
      const QSignalBlocker markersBlocker(markers);
      destinations->setChecked(config.getConcealmentEnabled());
      markers->setChecked(config.getConcealMarkdownMarkersEnabled());
    };
    refreshConcealmentActions();
    connect(p_menu, &QMenu::aboutToShow, this, refreshConcealmentActions);
    connect(destinations, &QAction::toggled, m_editorController,
            &MarkdownEditorController::setConcealmentEnabled);
    connect(markers, &QAction::toggled, m_editorController,
            &MarkdownEditorController::setConcealMarkdownMarkersEnabled);
  }

  {
    auto *sectionNumberAction = addAction(p_menu, ViewWindowToolBarHelper2::AllowAutoSectionNumber);
    sectionNumberAction->setChecked(m_outlineProvider->getAutoSectionNumberAllowed());
    connect(sectionNumberAction, &QAction::toggled, this, [this](bool p_checked) {
      m_outlineProvider->setAutoSectionNumberAllowed(p_checked);
      updateSectionNumberOptions();
      updateEditSectionNumberOptions(true);
      if (!p_checked) {
        removeSessionHeadingSectionNumbers();
      }
    });
  }

  {
    m_debugAction = addAction(p_menu, ViewWindowToolBarHelper2::Debug);
    connect(m_debugAction, &QAction::toggled, this, &MarkdownViewWindow2::setDebugVisible);
    m_debugAction->setEnabled(isReadMode() && !getBuffer().isEncrypted());
    m_debugAction->setVisible(isReadMode() && !getBuffer().isEncrypted());
    connect(this, &ViewWindow2::modeChanged, this, [this]() {
      m_debugAction->setEnabled(isReadMode() && !getBuffer().isEncrypted());
      m_debugAction->setVisible(isReadMode() && !getBuffer().isEncrypted());
    });
  }
}

void MarkdownViewWindow2::handlePrint() {
  setPresentationMode(false);
  if (getBuffer().isEncrypted()) {
    showMessage(tr("Printing encrypted notes is not supported"));
    return;
  }
  if (m_printPending || m_printer || !m_sourceAvailable || m_rebindingBuffer) {
    return;
  }
  cancelEditorImageView(true);
  const bool hasSelection = m_mode == ViewWindowMode::Edit
                                ? m_editor && m_editor->getTextEdit()->hasSelection()
                                : m_viewer && m_viewer->hasSelection();
  const QPointer<MarkdownViewWindow2> guard(this);
  const auto generation = getBufferGeneration();
  const auto printer = PrintUtils::promptForPrint(hasSelection, this);
  if (guard && generation == getBufferGeneration()) {
    printDocument(printer);
  }
}

void MarkdownViewWindow2::printDocument(const QSharedPointer<QPrinter> &p_printer) {
  if (!p_printer || m_printPending || m_printer || getBuffer().isEncrypted() || !isEnabled() ||
      !getBuffer().isValid() || !m_sourceAvailable || m_rebindingBuffer) {
    return;
  }
  if (m_readerResourcesSuspended && (!isVisible() || !resumeInactiveReader())) {
    return;
  }
  setPresentationMode(false);
  cancelEditorImageView(true);
  m_printer = p_printer;
  m_printBufferGeneration = getBufferGeneration();
  if (m_mode == ViewWindowMode::Edit) {
    if (!m_editor) {
      m_printer.reset();
      return;
    }
    const QPointer<MarkdownViewWindow2> guard(this);
    const auto generation = getBufferGeneration();
    m_editor->getTextEdit()->print(p_printer.get());
    if (!guard) {
      return;
    }
    if (generation != getBufferGeneration()) {
      if (m_printer == p_printer) {
        m_printer.reset();
      }
      return;
    }
    const auto state = p_printer->printerState();
    onPrintFinished(state != QPrinter::Error && state != QPrinter::Aborted);
    return;
  }
  if (!m_viewer && !setupViewer()) {
    m_printer.reset();
    return;
  }
  m_printPending = true;
  if (!m_viewerPageInitialized) {
    initializeViewerPage();
  }
  // Print the current draft, not the last debounce/save snapshot. The web work-finished
  // acknowledgement below waits for document rendering, not merely page readiness.
  if (m_editor) {
    requestEditorNativeRender();
  } else {
    requestNativeRender(getLatestContent());
  }
}

bool MarkdownViewWindow2::aboutToClose(bool p_force) {
  cancelEditorImageView();
  setPresentationMode(false);
  updateEditSectionNumberOptions(false);
  const bool result = ViewWindow2::aboutToClose(p_force);
  if (!result) {
    updateEditSectionNumberOptions(true);
    return false;
  }

  m_readerLifecycleClosing = true;
  m_inactiveReaderTimer->stop();
  m_inactiveReaderFileOpenSettings.reset();
  m_restoreReaderStatePending = false;
  invalidateNativeRender(true);
  m_nativeRender->revoke();
  m_webJobs->revoke();
  m_webJobHost->releaseProtectedSession();
  if (getBuffer().isEncrypted()) {
    releaseProtectedView();
    return true;
  }
  finalizeBufferImages();

  return true;
}

void MarkdownViewWindow2::finalizeBufferImages() {
  if (getBuffer().isEncrypted() || !isLastWindowForBuffer()) {
    return;
  }
  clearObsoleteImages();

  // Clear abandoned staged copies before finalizing the originals. This is an
  // accepted-close/replacement gate, never an auto-save or preview side effect.
  if (!m_pendingLegacyMigration.isEmpty() && m_legacyImageController) {
    if (auto *bufferSvc = getServices().get<BufferService>()) {
      const auto bufferId = getBuffer().id();
      m_legacyImageController->finalize(getBuffer(), m_pendingLegacyMigration,
                                        bufferSvc->isDirty(bufferId),
                                        bufferSvc->isSaveQueueBusy(bufferId));
    }
    m_pendingLegacyMigration.clear();
  }
}

// ============ setupTextEditor (lazy init) ============

void MarkdownViewWindow2::setupTextEditor() {
  Q_ASSERT(!m_editor);

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

  // Create editor with the same grammar used by native reading/export.
  auto documentConfig = MarkdownEditorController::buildMarkdownEditorConfigFromContent(
      editorConfig, mdConfig, themeContent, syntaxTheme, scaleFactor, maxContentWidth);
  documentConfig->m_documentParseOptions =
      MarkdownHtmlRenderer::parseOptions(nativeRenderOptions());
  m_editor = new MarkdownEditor(
      getServices(), mdConfig, documentConfig,
      MarkdownEditorController::buildMarkdownEditorParameters(editorConfig, mdConfig), this);
  updateEditSectionNumberOptions(false);

  // Insert at index 0 in splitter (editor always first).
  m_splitter->insertWidget(0, m_editor);

  // Connect editor signals.
  connectEditorSignals();
  TextViewWindowHelper::connectUndoRedoActions(this, m_editor->getTextEdit(), m_undoAction,
                                               m_redoAction);

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

  // Encrypted notes default to embedded Base64, not implicit image-host uploads.
  if (!getBuffer().isEncrypted() && m_imageHostController) {
    m_editor->setImageHostController(m_imageHostController);
  }

  // Apply config.
  updateEditorFromConfig();
  updateNativeDocumentRetention();
  connect(m_editor->getHighlighter(), &vte::MarkdownHighlighter::documentReady, this,
          [this](quint64 p_generation, const vte::MarkdownDocumentPtr &p_document) {
            if (m_waitingNativeDocument && p_generation == m_renderEditorGeneration &&
                nativeRenderSourceIsCurrent() && !m_readerResourcesSuspended &&
                !m_readerLifecycleClosing && !m_nativeLocking) {
              m_waitingNativeDocument = false;
              if (p_document) {
                renderNativeDocument(p_document);
              } else {
                showMessage(tr("Unable to parse Markdown"));
              }
            }
          });

  connect(m_editor, &MarkdownEditor::htmlToMarkdownRequested, m_webJobs,
          &MarkdownWebJobController::htmlToMarkdown);
  connect(m_webJobs, &MarkdownWebJobController::htmlToMarkdownReady, m_editor,
          &MarkdownEditor::handleHtmlToMarkdownData);
  connect(m_webJobs, &MarkdownWebJobController::highlightMathReady, m_editor,
          &MarkdownEditor::handleExternalMathHighlightData);

  // External display math ($$...$$) source highlighting pipeline.
  connect(m_editor, &MarkdownEditor::externalMathHighlightRequested, this,
          &MarkdownViewWindow2::handleExternalMathHighlightRequest);

  // Switch to read mode when editor requests it.
  // Save first to avoid losing unsaved changes (legacy: read(true) calls save).
  connect(m_editor, &MarkdownEditor::readRequested, this, [this]() {
    const QPointer<MarkdownViewWindow2> guard(this);
    const auto generation = getBufferGeneration();
    if (save() && guard && generation == getBufferGeneration()) {
      setMode(ViewWindowMode::Read);
    }
  });

  // Heading links use the same native ID table as reading, without starting WebEngine.
  m_editor->setHeadingLinkResolver([this](const QString &p_text, int p_line,
                                          std::function<void(bool, const QString &)> p_cb) {
    if (m_rebindingBuffer || !m_sourceAvailable || m_readerLifecycleClosing || m_nativeLocking) {
      p_cb(false, QString());
      return;
    }
    const QPointer<MarkdownViewWindow2> self(this);
    const auto generation = getBufferGeneration();
    const auto document = m_editor->getHighlighter()->currentDocument();
    const auto source = p_text.toUtf8();
    m_nativeRender->fetchHeadingAnchor(
        source, p_line, nativeRenderOptions(), document,
        [self, generation, p_cb](const MarkdownRenderController::HeadingAnchorResult &p_result) {
          if (!self || generation != self->getBufferGeneration() || self->m_rebindingBuffer ||
              !self->m_sourceAvailable || !p_result.found || !p_result.error.isEmpty() ||
              p_result.cancelled) {
            if (self && generation == self->getBufferGeneration() && !self->m_rebindingBuffer &&
                !p_result.cancelled && !p_result.error.isEmpty()) {
              self->showMessage(tr("Unable to resolve heading link"));
            }
            p_cb(false, QString());
            return;
          }
          p_cb(true, MarkdownEditorController::composeHeadingLink(self->getBuffer().resolvedPath(),
                                                                  p_result.anchor));
        });
  });

  applyReadableWidth();
  updateJobRenderContext();
}

bool MarkdownViewWindow2::setupViewer() {
  Q_ASSERT(!m_viewer);
  if (m_readerResourcesSuspended || m_readerLifecycleClosing) {
    return false;
  }

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mdConfig = editorConfig.getMarkdownEditorConfig();

  m_editorController->checkAndUpdateConfigRevision();

  // Update HTML template via HtmlTemplateService.
  auto *htmlTemplateService = getServices().get<HtmlTemplateService>();
  auto *themeService = getServices().get<ThemeService>();
  if (getBuffer().isEncrypted()) {
    m_protectedView.reset(new ProtectedView());
    auto *profiles = getServices().get<WebEngineProfileService>();
    if (profiles && htmlTemplateService) {
      m_protectedView->page = profiles->createProtectedProfile(getBuffer());
    }
    if (m_protectedView->page.profile) {
      QHash<QString, QByteArray> resources;
      const auto &page = m_protectedView->page;
      QColor background(themeService->optionalPaletteColor(QStringLiteral(
          "base#content#bg"))); // palette-token-optional: custom themes may omit this
      if (!background.isValid()) {
        background = themeService->getBaseBackground();
      }
      const auto html = htmlTemplateService->protectedMarkdownViewerTemplate(
          page.token, page.nonce, themeService->getCurrentTheme().name(),
          qGray(background.rgb()) < 128, mdConfig.getMathRenderer(), resources);
      for (auto it = resources.cbegin(); it != resources.cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("/app/themes/")) &&
            it.key().endsWith(QStringLiteral("/highlight.css"))) {
          m_protectedView->highlightStyleSheet = it.value();
          break;
        }
      }
      m_protectedView->documentPublished =
          profiles->setProtectedDocument(page.profile, html, resources);
    }
    if (!m_protectedView->documentPublished) {
      if (profiles) {
        profiles->revokeProtectedProfile(m_protectedView->page.profile);
      }
      delete m_protectedView->page.profile;
      m_protectedView.reset();
      showMessage(tr("Unable to open protected preview"));
      return false;
    }
  } else {
    htmlTemplateService->updateMarkdownViewerTemplate(
        mdConfig, themeService->fetchWebStyleSheet(),
        themeService->getFile(Theme::File::HighlightStyleSheet));
  }

  // Create adapter and viewer.
  auto *adapterObj = new MarkdownViewerAdapter(getServices(), this);

  auto bgColor = getServices().get<ThemeService>()->getBaseBackground();
  auto zoomFactor = m_rebindingBuffer && !m_inactiveReaderState.m_splitterSizes.isEmpty()
                        ? m_inactiveReaderState.m_zoomFactor
                        : configMgr->getSessionConfig().getMarkdownZoomFactorInReadMode();

  m_viewer = new MarkdownViewer(adapterObj, this, getServices(), bgColor, zoomFactor, this,
                                m_protectedView ? m_protectedView->page.profile : nullptr);
  if (m_protectedView) {
    auto *profiles = getServices().get<WebEngineProfileService>();
    auto *profile = m_protectedView->page.profile;
    adapterObj->setProtectedTypstAuthorization(m_protectedView->page.isAuthorized);
    connect(profiles, &WebEngineProfileService::protectedProfileRevoked, adapterObj,
            [adapterObj, profile](QWebEngineProfile *p_profile) {
              if (p_profile == profile)
                adapterObj->revokeProtectedTypst();
            });
    connect(profile, &QObject::destroyed, adapterObj, &MarkdownViewerAdapter::revokeProtectedTypst);
    connect(profiles, &QObject::destroyed, adapterObj,
            &MarkdownViewerAdapter::revokeProtectedTypst);
  }
  m_viewer->setController(m_windowController);
  updateSectionNumberOptions();

  m_splitter->addWidget(m_viewer);
  // Start hidden. The viewer will be shown explicitly when entering
  // Read mode or EditPreview mode. This avoids a visible blank pane
  // in the QSplitter while WebEngine loads the HTML template.
  m_viewer->hide();

  m_viewerPageInitialized = false;
  m_viewerBufferRevision = -1;
  m_webJobs->setPreferredSession(m_viewer->jobSession(), m_protectedView
                                                             ? m_protectedView->highlightStyleSheet
                                                             : QByteArray());

  if (m_protectedView) {
    auto *webPage = qobject_cast<WebPage *>(m_viewer->page());
    webPage->setProtectedDocumentUrl(m_protectedView->page.url);
    webPage->setSensitiveContent(true);
  }
  // Keep a live printer until WebEngine finishes, even if its document was replaced.
  const QPointer<MarkdownViewWindow2> guard(this);
  connect(m_viewer, &MarkdownViewer::printFinished, m_viewer, [guard](bool p_ok) {
    if (guard) {
      guard->onPrintFinished(p_ok);
    }
  });
  connectViewerSignals();
  return true;
}

void MarkdownViewWindow2::connectViewerSignals() {
  const QPointer<MarkdownViewer> viewer(m_viewer);
  auto *adapterObj = adapter();
  const auto generation = getBufferGeneration();
  const auto current = [this, viewer, generation]() {
    return generation == getBufferGeneration() && !m_readerResourcesSuspended &&
           !m_readerLifecycleClosing && viewer && m_viewer == viewer.data();
  };
  connect(adapterObj, &MarkdownViewerAdapter::linkRequested, this,
          [this, current](const QString &p_url) {
            if (current() && m_viewerReady) {
              handleOpenFileRequest(p_url, false);
            }
          });

  // Zoom persistence.
  connect(m_viewer, &MarkdownViewer::zoomFactorChanged, this, [this, current](qreal p_factor) {
    if (!current()) {
      return;
    }
    m_editorController->persistViewerZoomFactor(p_factor);
    showZoomFactor(p_factor);
  });

  // Link hover status message.
  connect(m_viewer, &WebViewer::linkHovered, this, [this, current](const QString &p_url) {
    if (current() && m_viewerReady) {
      showMessage(p_url);
    }
  });

  // Edit request from viewer (double-click or context menu).
  connect(m_viewer, &MarkdownViewer::editRequested, this, [this, current]() {
    if (current() && m_viewerReady) {
      setMode(ViewWindowMode::Edit);
    }
  });

  // Export request from viewer context menu; forward the inherited signal.
  connect(m_viewer, &MarkdownViewer::exportRequested, this, [this, current]() {
    if (current() && m_viewerReady) {
      emit exportRequested();
    }
  });

  // Viewer find text result.
  connect(adapterObj, &MarkdownViewerAdapter::findTextReady, this,
          [this, current](const QStringList &p_texts, int p_totalMatches, int p_currentMatchIndex) {
            if (current() && m_viewerReady) {
              showFindResult(p_texts, p_totalMatches, p_currentMatchIndex);
            }
          });

  connect(adapterObj, &MarkdownViewerAdapter::imageViewStateChanged, this,
          [this, current](quint64 p_id, bool p_opened) {
            if (current()) {
              handleImageViewStateChanged(p_id, p_opened);
            }
          });
  connect(adapterObj, &MarkdownViewerAdapter::imageViewCancelled, this,
          [this, current](quint64 p_id) {
            if (current() && p_id == m_editorImageRequest) {
              cancelEditorImageView();
            }
          });
  connect(adapterObj, &MarkdownViewerAdapter::presentationStateChanged, this,
          [this, current](bool p_active, const QString &p_error) {
            if (current() && m_viewerReady) {
              handlePresentationStateChanged(p_active, p_error);
            }
          });
  connect(adapterObj, &MarkdownViewerAdapter::htmlUpdated, this, [this, current]() {
    if (current() && m_presentationRequestSent) {
      setPresentationMode(false);
    }
  });
  connect(m_viewer->page(), &QWebEnginePage::loadStarted, this, [this, current]() {
    if (!current()) {
      return;
    }
    const bool wasReady = m_viewerReady;
    m_viewerReady = false;
    // A requested Edit -> Read transition may still be loading its initial page.
    if (!m_preparingPresentation && (wasReady || m_presentationRequestSent)) {
      setPresentationMode(false);
    }
  });
  connect(m_viewer->page(), &QWebEnginePage::renderProcessTerminated, this, [this, current]() {
    if (!current()) {
      return;
    }
    m_viewerReady = false;
    m_viewerPageInitialized = false;
    setPresentationMode(false);
    cancelEditorImageView(true);
    if (m_mode == ViewWindowMode::Edit) {
      setEditViewMode(MarkdownEditorConfig::EditViewMode::EditOnly);
    }
    showMessage(tr("Preview renderer stopped"));
    if (m_printPending) {
      m_printPending = false;
      m_printer.reset();
      showMessage(tr("Unable to prepare note for printing"));
    }
  });
  connect(m_viewer->page(), &QWebEnginePage::loadingChanged, this,
          [this, current](const QWebEngineLoadingInfo &p_info) {
            // Replacing a still-loading document stops the outgoing navigation.
            // Its late stop is not a failure of the incoming page.
            if (!current() || p_info.status() != QWebEngineLoadingInfo::LoadFailedStatus) {
              return;
            }
            m_viewerReady = false;
            m_viewerPageInitialized = false;
            cancelEditorImageView(true);
            if (m_mode == ViewWindowMode::Edit) {
              setEditViewMode(MarkdownEditorConfig::EditViewMode::EditOnly);
            }
            showMessage(tr("Unable to load preview"));
            if (m_printPending) {
              m_printPending = false;
              m_printer.reset();
              showMessage(tr("Unable to prepare note for printing"));
            }
            if (m_presentationRequested) {
              qWarning() << "Markdown presentation failed: viewer page did not load";
              setPresentationMode(false);
              showMessage(tr("Unable to start presentation"));
            }
          });

  // Viewer ready signal.
  connect(adapterObj, &MarkdownViewerAdapter::ready, this, [this, current]() {
    if (!current() || !m_sourceAvailable || !m_viewerPageInitialized) {
      return;
    }
    m_viewerReady = true;
    applyReadableWidth();
    if (isReadMode()) {
      m_viewer->show();
    }
    if (m_mode == ViewWindowMode::Edit && !m_editorImageRequest) {
      setEditViewMode(m_editViewMode);
    }
    if (!m_editorImageRequest && m_protectedView &&
        !m_protectedView->splitterSizesOnReady.isEmpty()) {
      // Live-preview readiness distributes panes; preserve a theme reload's ratio afterward.
      m_splitter->setSizes(m_protectedView->splitterSizesOnReady);
      m_protectedView->splitterSizesOnReady.clear();
    }
    if (m_restoreReaderStatePending && !m_inactiveReaderState.m_splitterSizes.isEmpty()) {
      m_splitter->setSizes(m_inactiveReaderState.m_splitterSizes);
    }
    requestPresentationWhenReady();
  });

  // Deferred anchor scroll: drain pending anchor after rendering completes.
  connect(adapterObj, &MarkdownViewerAdapter::workFinished, this, [this, current]() {
    if (!current() || !m_viewerReady || !adapter()->isDocumentReady() ||
        !m_nativeRender->isCurrent(adapter()->documentGeneration()) ||
        !nativeRenderSourceIsCurrent()) {
      return;
    }
    requestPresentationWhenReady();
    if (m_printPending) {
      m_printPending = false;
      if (m_printer && m_printBufferGeneration == getBufferGeneration() && isEnabled() &&
          isVisible()) {
        m_viewer->print(m_printer.get());
      } else {
        m_printer.reset();
      }
    }
    if (m_restoreReaderStatePending) {
      m_restoreReaderStatePending = false;
      auto settings = std::move(m_inactiveReaderFileOpenSettings);
      m_inactiveReaderFileOpenSettings.reset();
      const bool explicitPosition =
          settings && (!settings->m_anchor.isEmpty() || settings->m_lineNumber >= 0 ||
                       (settings->m_searchHighlight.m_isValid &&
                        settings->m_searchHighlight.m_currentMatchLine >= 0));
      if (settings) {
        applyFileOpenSettings(*settings);
      }
      if (!explicitPosition && m_pendingAnchor.isEmpty() &&
          m_inactiveReaderState.m_topLineNumber >= 0) {
        adapter()->scrollToPosition(
            MarkdownViewerAdapter::Position(m_inactiveReaderState.m_topLineNumber, QString()));
      }
    }
    if (!m_pendingAnchor.isEmpty() && isReadMode() && adapter()) {
      adapter()->scrollToPosition(MarkdownViewerAdapter::Position(-1, m_pendingAnchor));
      m_pendingAnchor.clear();
    }
  });

  // Task list checkbox toggle from the preview.
  connect(adapterObj, &MarkdownViewerAdapter::taskListItemToggleRequested, this,
          [this, current](const QString &p_token, int p_line, bool p_checked) {
            if (current() && m_viewerReady && m_sourceAvailable && !m_rebindingBuffer) {
              onTaskListItemToggleRequested(p_token, p_line, p_checked);
            }
          });

  // Outline pipeline: viewer headings -> OutlineProvider.
  connect(adapterObj, &MarkdownViewerAdapter::headingsChanged, this, [this, current]() {
    if (!current() || !m_viewerReady) {
      return;
    }
    if (isReadMode()) {
      auto outline = headingsToOutline(adapter()->getHeadings());
      outline->m_hasSectionNumber = adapter()->getHeadingsHaveSectionNumber();
      m_outlineProvider->setOutline(outline);
    }
  });
  connect(adapterObj, &MarkdownViewerAdapter::currentHeadingChanged, this, [this, current]() {
    if (!current() || !m_viewerReady) {
      return;
    }
    if (isReadMode()) {
      m_outlineProvider->setCurrentHeadingIndex(adapter()->getCurrentHeadingIndex());
    }
  });

  // Read-mode link interception: open internal files via BufferService with anchor.
  connect(m_viewer, &WebViewer::localFileOpenRequested, this, [this, current](const QUrl &p_url) {
    if (!current() || !m_viewerReady || !p_url.isLocalFile()) {
      return;
    }
    QString localPath = p_url.toLocalFile();
    QString fragment = p_url.fragment();
    QString combined = localPath;
    if (!fragment.isEmpty()) {
      combined += QLatin1Char('#') + fragment;
    }
    handleOpenFileRequest(combined, false);
  });

  // Read-mode external link interception: route through the same policy sink as
  // edit mode so the security prompt lives in one place.
  connect(m_viewer, &WebViewer::externalLinkRequested, this, [this, current](const QUrl &p_url) {
    if (current() && m_viewerReady) {
      handleOpenFileRequest(p_url.toString(), false);
    }
  });
  connect(m_viewer, &MarkdownViewer::contextLinkOpenRequested, this,
          [this, current](const QString &p_link) {
            if (current() && m_viewerReady) {
              handleOpenFileRequest(p_link, true);
            }
          });
}

void MarkdownViewWindow2::updateJobRenderContext() {
  if (m_editor) {
    auto *edit = m_editor->getTextEdit();
    m_webJobs->setRenderContext(edit->devicePixelRatioF(), edit->viewport()->width());
  }
}

void MarkdownViewWindow2::updateInactiveReaderTimer(bool p_restart) {
  const auto timeout = getServices()
                           .get<ConfigMgr2>()
                           ->getEditorConfig()
                           .getMarkdownEditorConfig()
                           .getInactiveReaderTimeoutMinutes();
  p_restart |= timeout != m_inactiveReaderTimeoutMinutes;
  m_inactiveReaderTimeoutMinutes = timeout;
  if (isVisible() || !isEnabled() || m_readerLifecycleClosing || m_readerResourcesSuspended ||
      m_rebindingBuffer || !m_sourceAvailable || !getBuffer().isValid() || timeout <= 0) {
    m_inactiveReaderTimer->stop();
    return;
  }
  if (p_restart || !m_inactiveReaderTimer->isActive()) {
    m_inactiveReaderTimer->start(timeout * 60 * 1000);
  }
}

void MarkdownViewWindow2::unloadInactiveReader() {
  if (isVisible() || !isEnabled() || m_readerLifecycleClosing || m_readerResourcesSuspended ||
      m_rebindingBuffer || !m_sourceAvailable || !getBuffer().isValid() ||
      m_inactiveReaderTimeoutMinutes <= 0) {
    return;
  }
  if (m_printPending || m_printer || m_switchingMode) {
    updateInactiveReaderTimer(true);
    return;
  }

  // Block reentrant refresh before retiring any endpoint or native preview cache.
  m_readerResourcesSuspended = true;
  m_inactiveReaderTimer->stop();
  if (m_syncPreviewTimer) {
    m_syncPreviewTimer->stop();
  }
  cancelEditorImageView();
  setPresentationMode(false);
  setDebugVisible(false);
  delete m_debugViewer;
  m_debugViewer = nullptr;
  if (m_viewer && !m_restoreReaderStatePending) {
    m_inactiveReaderState.m_topLineNumber = adapter()->getTopLineNumber();
    m_inactiveReaderState.m_zoomFactor = m_viewer->zoomFactor();
    m_inactiveReaderState.m_splitterSizes = m_splitter->sizes();
  }
  m_viewerReady = false;
  m_viewerPageInitialized = false;
  m_viewerBufferRevision = -1;
  ++m_navigationGeneration;
  if (m_viewer) {
    m_viewer->invalidateNavigationTargets();
  }
  invalidateWebJobs(true);
  retireReader();
  m_deferredPreviewUpdate = false;
}

bool MarkdownViewWindow2::resumeInactiveReader() {
  if (!m_readerResourcesSuspended) {
    return true;
  }
  if (m_readerLifecycleClosing || m_rebindingBuffer || !m_sourceAvailable || !isEnabled() ||
      !isVisible() || !getBuffer().isValid() ||
      (getBuffer().isEncrypted() && !getBuffer().acquireProtectedLease())) {
    return false;
  }
  m_readerResourcesSuspended = false;
  m_webJobs->setSuspended(false);
  m_nativeRender->setSuspended(false);
  updateNativeDocumentRetention();
  const bool needsReader =
      isReadMode() || m_editViewMode == MarkdownEditorConfig::EditViewMode::EditPreview;
  if (needsReader) {
    m_restoreReaderStatePending = true;
    const auto pendingAnchor = m_pendingAnchor;
    if (isReadMode()) {
      if (setupViewer()) {
        m_viewer->setZoomFactor(m_inactiveReaderState.m_zoomFactor);
        syncViewerFromBuffer(false);
        m_viewer->show();
      }
    } else {
      setEditViewMode(m_editViewMode);
      if (m_viewer) {
        m_viewer->setZoomFactor(m_inactiveReaderState.m_zoomFactor);
      }
    }
    m_pendingAnchor = pendingAnchor;
    if (!m_viewer) {
      m_readerResourcesSuspended = true;
      invalidateWebJobs(true);
      return false;
    }
  } else {
    // Native editing resumes without a document reader or a synthetic ready event.
    m_restoreReaderStatePending = false;
    if (m_inactiveReaderFileOpenSettings) {
      const auto settings = std::move(*m_inactiveReaderFileOpenSettings);
      m_inactiveReaderFileOpenSettings.reset();
      applyFileOpenSettings(settings);
    }
  }
  refreshEditorWebJobs();
  return true;
}

void MarkdownViewWindow2::invalidateWebJobs(bool p_suspend) {
  invalidateNativeRender(p_suspend);
  if (m_printPending) {
    m_printPending = false;
    m_printer.reset();
  }
  ++m_highlightStyleGeneration;
  m_codeBlockStylesInitialized = false;
  m_codeBlockStylesPending = false;
  m_webJobs->setPreferredSession(nullptr);
  if (p_suspend) {
    m_webJobs->setSuspended(true);
  } else {
    m_webJobs->invalidate();
  }
  m_webJobHost->invalidate();
  if (m_previewHelper) {
    m_previewHelper->invalidatePreviews();
  }
  if (m_syncPreviewTimer) {
    m_syncPreviewTimer->stop();
  }
}

void MarkdownViewWindow2::refreshEditorWebJobs() {
  if (m_editor && m_sourceAvailable && !m_rebindingBuffer && isEnabled() &&
      !m_readerResourcesSuspended) {
    updateJobRenderContext();
    ensureExternalHighlightStyles();
    m_editor->refreshPreviewHighlight();
  }
}

bool MarkdownViewWindow2::reloadProtectedViewer() {
  cancelEditorImageView();
  Q_ASSERT(m_viewer && m_protectedView);
  if (m_readerResourcesSuspended) {
    return false;
  }
  const auto position = capturePositionState();
  const auto sizes = m_splitter->sizes();
  auto *oldViewer = m_viewer;
  const auto zoomFactor = oldViewer->zoomFactor();
  const bool visible = !oldViewer->isHidden();
  const bool focused =
      oldViewer->hasFocus() || oldViewer->isAncestorOf(QApplication::focusWidget());
  const bool wasReady = oldViewer->adapter()->isReady();
  QScopedPointer<ProtectedView> oldProtectedView;
  oldProtectedView.swap(m_protectedView);
  m_viewer = nullptr;

  // Profiles are immutable document generations. Keep the old one usable until
  // its replacement is published; never destroy the editor or its undo history.
  if (!setupViewer()) {
    m_viewer = oldViewer;
    oldProtectedView.swap(m_protectedView);
    m_viewerReady = wasReady;
    // The retained page belongs to the old renderer generation, not new assistance work.
    return false;
  }
  ++m_navigationGeneration;
  getServices().get<WebEngineProfileService>()->revokeProtectedProfile(
      oldProtectedView->page.profile);
  delete oldViewer;
  delete oldProtectedView->page.profile;

  m_viewer->setZoomFactor(zoomFactor);
  m_viewer->setVisible(visible);
  m_splitter->setSizes(sizes);
  m_protectedView->splitterSizesOnReady = sizes;
  if (focused) {
    m_viewer->setFocus();
  }
  syncViewerFromBuffer(false);
  restorePositionState(position);
  return true;
}

// ============ setupPreviewHelper ============

void MarkdownViewWindow2::setupPreviewHelper() {
  Q_ASSERT(!m_previewHelper);
  m_previewHelper = new PreviewHelper(nullptr, this);
  connect(m_previewHelper, &PreviewHelper::graphPreviewRequested, this,
          [this](quint64 p_id, TimeStamp p_stamp, const QString &p_language,
                 const QString &p_source, qreal p_scale) {
            updateJobRenderContext();
            m_webJobs->previewGraph(p_id, p_stamp, p_language, p_source, p_scale);
          });
  connect(m_previewHelper, &PreviewHelper::mathPreviewRequested, this,
          [this](quint64 p_id, TimeStamp p_stamp, const QString &p_source, qreal p_scale) {
            updateJobRenderContext();
            m_webJobs->previewMath(p_id, p_stamp, p_source, p_scale);
          });
  connect(m_webJobs, &MarkdownWebJobController::graphPreviewDataReady, m_previewHelper,
          &PreviewHelper::handleGraphPreviewData);
  connect(m_webJobs, &MarkdownWebJobController::mathPreviewDataReady, m_previewHelper,
          &PreviewHelper::handleMathPreviewData);
  if (getBuffer().isEncrypted()) {
    m_previewHelper->setProtectedView(true);
  }

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

  connect(m_outlineProvider.data(), &OutlineProvider::moveRequested, this,
          [this](int p_sourceHeadingIndex, int p_beforeHeadingIndex, int p_targetLevel) {
            if (m_mode != ViewWindowMode::Edit || !m_editor || m_editor->isReadOnly()) {
              return;
            }

            const auto &buffer = getBuffer();
            if (!buffer.isValid() || buffer.isReadOnly()) {
              return;
            }

            const auto &editorHeadings = m_editor->getHeadings();
            if (p_sourceHeadingIndex < 0 || p_sourceHeadingIndex >= editorHeadings.size() ||
                p_beforeHeadingIndex < -1 || p_beforeHeadingIndex >= editorHeadings.size()) {
              return;
            }

            QVector<MarkdownEditorController::HeadingBlockInfo> headings;
            headings.reserve(editorHeadings.size());
            for (const auto &heading : editorHeadings) {
              MarkdownEditorController::HeadingBlockInfo info;
              info.level = heading.m_level;
              info.startPos = heading.m_startPos;
              info.endPos = heading.m_endPos;
              headings.append(info);
            }

            auto *textEdit = m_editor->getTextEdit();
            const QTextCursor cursor = textEdit->textCursor();
            int selectionStart = -1;
            int selectionEnd = -1;
            const auto &selection = textEdit->getSelection();
            if (selection.isValid() &&
                (!cursor.hasSelection() || cursor.selectionStart() != selection.start() ||
                 cursor.selectionEnd() != selection.end())) {
              selectionStart = selection.start();
              selectionEnd = selection.end();
            }

            const auto moveResult = MarkdownEditorController::reorderHeadingBlock(
                textEdit->document(), headings, p_sourceHeadingIndex, p_beforeHeadingIndex,
                p_targetLevel, cursor.position(), cursor.anchor(), selectionStart, selectionEnd);
            if (!moveResult.moved) {
              return;
            }

            QTextCursor restoredCursor(textEdit->document());
            restoredCursor.setPosition(moveResult.cursorAnchor);
            restoredCursor.setPosition(moveResult.cursorPosition, QTextCursor::KeepAnchor);
            textEdit->setTextCursor(restoredCursor);
            if (moveResult.selectionStart >= 0 &&
                moveResult.selectionEnd > moveResult.selectionStart) {
              textEdit->setOverriddenSelection(moveResult.selectionStart, moveResult.selectionEnd);
            }
            focusEditor();
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
      outline->m_headings.last().m_isPlaceholder = heading.m_isPlaceholder;
    }
  }

  return outline;
}

// ============ connectEditorSignals ============

void MarkdownViewWindow2::requestEditorImageView(const QString &p_url) {
  cancelEditorImageView();
  if (m_mode != ViewWindowMode::Edit || !m_editor || !m_sourceAvailable || m_rebindingBuffer ||
      !isEnabled() || !isVisible()) {
    return;
  }
  if (!resumeInactiveReader()) {
    return;
  }
  if (!m_viewer && !setupViewer()) {
    return;
  }
  if (!m_viewerPageInitialized) {
    // An empty runtime is sufficient for a resolved image URL. Do not render the note.
    initializeViewerPage();
  }
  // The source URL is already resolved. Do not rebuild the note merely to view it.
  if (m_syncPreviewTimer) {
    m_deferredPreviewUpdate |= m_syncPreviewTimer->isActive() &&
                               m_editViewMode == MarkdownEditorConfig::EditViewMode::EditPreview;
    m_syncPreviewTimer->stop();
  }
  m_imageSplitterSizes = m_splitter->sizes();
  m_imageEditorVisible = !m_editor->isHidden();
  m_imageViewerVisible = !m_viewer->isHidden();
  m_imageHandleVisible = m_splitter->count() > 1 && !m_splitter->handle(1)->isHidden();
  auto *focus = QApplication::focusWidget();
  m_imageOriginalFocus = focus && (focus == m_editor || m_editor->isAncestorOf(focus))
                             ? focus
                             : m_editor->getTextEdit();
  QSizeF viewportHint;
  if (m_editViewMode == MarkdownEditorConfig::EditViewMode::EditOnly) {
    const auto size = m_splitter->contentsRect().size();
    m_viewer->resize(size);
    viewportHint = QSizeF(size) / m_viewer->zoomFactor();
  }
  m_editorImageRequest = adapter()->requestImageView(p_url, viewportHint);
  qApp->installEventFilter(this);
}

void MarkdownViewWindow2::handleImageViewStateChanged(quint64 p_id, bool p_opened) {
  if (!m_editorImageRequest || p_id != m_editorImageRequest) {
    return;
  }
  if (!p_opened) {
    cancelEditorImageView(true);
    return;
  }
  if (m_mode != ViewWindowMode::Edit || !isVisible() || !isEnabled()) {
    cancelEditorImageView();
    return;
  }
  m_editorImageOpened = true;
  if (m_editViewMode == MarkdownEditorConfig::EditViewMode::EditOnly) {
    // Hide first: showing both panes would briefly resize the prepared image to half width.
    const bool updatesEnabled = m_splitter->updatesEnabled();
    m_splitter->setUpdatesEnabled(false);
    m_editor->hide();
    if (m_splitter->count() > 1) {
      m_splitter->handle(1)->hide();
    }
    m_viewer->show();
    m_splitter->setUpdatesEnabled(updatesEnabled);
  }
  m_viewer->setFocus();
}

void MarkdownViewWindow2::cancelEditorImageView(bool p_restoreFocus) {
  const bool restore = m_editorImageRequest != 0;
  const auto focus = m_imageOriginalFocus;
  // Invalidate before calling the adapter: cancellation may synchronously signal back.
  m_editorImageRequest = 0;
  m_editorImageOpened = false;
  m_imageOriginalFocus.clear();
  if (!m_presentationRequested) {
    qApp->removeEventFilter(this);
  }
  if (adapter()) {
    adapter()->cancelImageView();
  }
  if (restore && m_editor && m_viewer) {
    // Child visibility may be restored under a hidden tab; never show its parent.
    const bool updatesEnabled = m_splitter->updatesEnabled();
    m_splitter->setUpdatesEnabled(false);
    if (!m_imageViewerVisible) {
      m_viewer->hide();
    }
    m_editor->setVisible(m_imageEditorVisible);
    m_viewer->setVisible(m_imageViewerVisible);
    if (m_splitter->count() > 1) {
      m_splitter->handle(1)->setVisible(m_imageHandleVisible);
    }
    m_splitter->setSizes(m_imageSplitterSizes);
    m_splitter->setUpdatesEnabled(updatesEnabled);
    if (p_restoreFocus && isVisible() && isEnabled() && focus && focus->isVisible()) {
      focus->setFocus(Qt::OtherFocusReason);
    }
  }
  m_imageSplitterSizes.clear();
  if (p_restoreFocus) {
    resumeDeferredPreviewUpdate();
  }
}

void MarkdownViewWindow2::resumeDeferredPreviewUpdate() {
  if (m_deferredPreviewUpdate && !m_editorImageRequest && m_editor && m_viewer &&
      m_syncPreviewTimer && !m_syncPreviewTimer->isActive() && isVisible() && isEnabled() &&
      m_mode == ViewWindowMode::Edit &&
      m_editViewMode == MarkdownEditorConfig::EditViewMode::EditPreview) {
    m_syncPreviewTimer->start();
  }
}

void MarkdownViewWindow2::connectEditorSignals() {
  connect(m_editor, &MarkdownEditor::imageViewRequested, this,
          &MarkdownViewWindow2::requestEditorImageView);
  // Focus forwarding.
  connect(m_editor, &vte::VTextEditor::focusIn, this, [this]() { emit focused(this); });

  // Content change -> auto-save dirty flag.
  connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, this, [this]() {
    if (!m_applyingTaskToggle && m_renderUsesEditor &&
        m_editor->getHighlighter()->documentGeneration() != m_renderEditorGeneration) {
      invalidateNativeRender();
    }
    if (m_propagateEditorToBuffer && m_sourceAvailable && !m_rebindingBuffer) {
      onEditorContentsChanged();
    }
  });

  connect(m_editor, &MarkdownEditor::headingsChanged, this, [this]() {
    if (m_mode != ViewWindowMode::Edit || !m_sourceAvailable || m_rebindingBuffer) {
      return;
    }

    const auto &editorHeadings = m_editor->getHeadings();
    auto outline = headingsToOutline(editorHeadings);
    outline->m_hasSectionNumber = m_editor->getHeadingsHaveSectionNumber();
    for (int i = 0; i < editorHeadings.size(); ++i) {
      outline->m_headings[i].m_reorderable =
          editorHeadings[i].m_startPos >= 0 &&
          editorHeadings[i].m_endPos > editorHeadings[i].m_startPos;
    }

    const auto &buffer = getBuffer();
    outline->m_reorderSupported =
        !m_editor->isReadOnly() && buffer.isValid() && !buffer.isReadOnly();
    m_outlineProvider->setOutline(outline);
  });
  connect(m_editor, &MarkdownEditor::currentHeadingChanged, this, [this]() {
    if (!isReadMode() && m_sourceAvailable && !m_rebindingBuffer) {
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
          [this](const QString &p_filePath, bool p_fromContextMenu) {
            handleOpenFileRequest(p_filePath, p_fromContextMenu);
          });
}

void MarkdownViewWindow2::handleOpenFileRequest(const QString &p_filePath, bool p_fromContextMenu) {
  if (!m_sourceAvailable || m_rebindingBuffer || !isEnabled()) {
    return;
  }
  if (p_filePath.startsWith(QLatin1Char('#'))) {
    handleAnchorJump(p_filePath.mid(1));
    return;
  }

  // External links (http/https/ftp/mailto) are opened by the system. This is the
  // single policy sink shared by edit-mode (direct click, context-menu "Open Link")
  // and read-mode navigation, so the security prompt lives in one place.
  const QUrl url(p_filePath);
  const auto scheme = url.scheme();
  if (getBuffer().isEncrypted()) {
    if ((!scheme.isEmpty() && scheme != QStringLiteral("http") &&
         scheme != QStringLiteral("https")) ||
        p_filePath.startsWith(QLatin1Char('/')) || p_filePath.startsWith(QLatin1Char('\\'))) {
      return;
    }
  }
  if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https") ||
      scheme == QStringLiteral("ftp") || scheme == QStringLiteral("mailto")) {
    const QPointer<MarkdownViewWindow2> guard(this);
    const auto generation = getBufferGeneration();
    const auto confirmation =
        ExternalLinkConfirmation::ask(url, getServices().get<ConfigMgr2>(), this);
    if (!guard || generation != getBufferGeneration() || m_rebindingBuffer) {
      return;
    }
    if (confirmation.accept()) {
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
    if (getBuffer().isEncrypted()) {
      showMessage(tr("Protected note links can open only indexed notebook notes"));
      return;
    }
    qWarning() << "File not in any notebook:" << absPath;
    WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(absPath));
    return;
  }

  NodeIdentifier nodeId;
  nodeId.notebookId = resolved[QLatin1String(vxcore::kJsonKeyNotebookId)].toString();
  nodeId.relativePath = resolved[QStringLiteral("relativePath")].toString();

  if (getBuffer().isEncrypted() &&
      nbSvc->getFileInfo(nodeId.notebookId, nodeId.relativePath).isEmpty()) {
    showMessage(tr("Protected note links can open only indexed notebook notes"));
    return;
  }
  auto settings = getLinkOpenSettings(p_fromContextMenu);
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

void MarkdownViewWindow2::setMode(ViewWindowMode p_mode) {
  if (!m_rebindingBuffer) {
    setModeInternal(p_mode, true);
    return;
  }
  if (m_switchingMode) {
    return;
  }
  const QPointer<MarkdownViewWindow2> guard(this);
  const auto generation = getBufferGeneration();
  m_restoreReaderStatePending =
      p_mode == m_mode &&
      (p_mode == ViewWindowMode::Read ||
       m_editViewMode == MarkdownEditorConfig::EditViewMode::EditPreview) &&
      !m_inactiveReaderState.m_splitterSizes.isEmpty();
  // This is a new document, not an Edit -> Read sync of the outgoing writer.
  m_mode = ViewWindowMode::Invalid;
  m_previousMode = ViewWindowMode::Invalid;
  setModeInternal(p_mode, true);
  if (!guard || generation != getBufferGeneration()) {
    return;
  }
  m_rebindingBuffer = false;
  if (m_sourceAvailable) {
    m_nativeRender->setSuspended(false);
    updateNativeDocumentRetention();
    // Earlier mode setup installed the page but could not admit work while rebinding.
    if (isReadMode() || m_editViewMode == MarkdownEditorConfig::EditViewMode::EditPreview) {
      syncViewerFromBuffer(false);
    }
    if (m_editor) {
      m_editor->setInplacePreviewEnabled(m_inplacePreviewEnabled);
    }
    snapshotInitialImages();
    m_webJobs->setSuspended(false);
    refreshEditorWebJobs();
  } else {
    showMessage(tr("Unable to load note content"));
  }
  updateInactiveReaderTimer(true);
}

void MarkdownViewWindow2::fetchNavigationTargets(NavigationTargetsCallback p_callback) {
  const auto generation = ++m_navigationGeneration;
  if (!p_callback) {
    return;
  }
  const QPointer<MarkdownViewWindow2> self(this);
  const QPointer<MarkdownEditor> editor(m_editor);
  const QPointer<MarkdownViewer> viewer(m_viewer);
  const auto mode = m_mode;
  const auto editMode = m_editViewMode;
  const auto bufferGeneration = getBufferGeneration();
  const auto current = [self, editor, viewer, generation, bufferGeneration, mode, editMode]() {
    return self && self->getBufferGeneration() == bufferGeneration && self->m_sourceAvailable &&
           !self->m_rebindingBuffer && self->isVisible() && self->isEnabled() &&
           !self->m_switchingMode && self->m_navigationGeneration == generation &&
           self->m_mode == mode && self->m_editViewMode == editMode &&
           self->m_editor == editor.data() && self->m_viewer == viewer.data();
  };
  if (!current()) {
    p_callback({});
    return;
  }

  QVector<NavigationTarget> editorTargets;
  if (mode == ViewWindowMode::Edit && editor && editor->isVisible()) {
    editorTargets = editor->getNavigationTargets();
  }
  auto complete = [self, current, editorTargets = std::move(editorTargets),
                   p_callback =
                       std::move(p_callback)](QVector<NavigationTarget> p_targets) mutable {
    if (!current()) {
      p_callback({});
      return;
    }
    editorTargets.reserve(editorTargets.size() + p_targets.size());
    for (auto &target : p_targets) {
      editorTargets.append(std::move(target));
    }
    editorTargets.erase(std::remove_if(editorTargets.begin(), editorTargets.end(),
                                       [self](const NavigationTarget &p_target) {
                                         return !p_target.m_widget ||
                                                !p_target.m_widget->isVisible() ||
                                                !self->isAncestorOf(p_target.m_widget) ||
                                                p_target.m_rect.isEmpty() || !p_target.m_activate;
                                       }),
                        editorTargets.end());
    std::stable_sort(editorTargets.begin(), editorTargets.end(),
                     [self](const NavigationTarget &p_left, const NavigationTarget &p_right) {
                       const auto left = p_left.m_widget->mapTo(self, p_left.m_rect.topLeft());
                       const auto right = p_right.m_widget->mapTo(self, p_right.m_rect.topLeft());
                       return left.y() == right.y() ? left.x() < right.x() : left.y() < right.y();
                     });
    for (auto &target : editorTargets) {
      auto activate = std::move(target.m_activate);
      target.m_activate = [current, activate = std::move(activate)]() {
        if (current()) {
          activate();
        }
      };
    }
    p_callback(std::move(editorTargets));
  };
  if (viewer && viewer->isVisible() && m_viewerReady &&
      (mode == ViewWindowMode::Read ||
       (mode == ViewWindowMode::Edit &&
        editMode == MarkdownEditorConfig::EditViewMode::EditPreview))) {
    viewer->fetchNavigationTargets(std::move(complete));
  } else {
    complete({});
  }
}

static void syncEditorPositionFromReadMode(MarkdownEditor *p_editor, int p_lineNumber) {
  if (p_lineNumber < 0) {
    return;
  }

  // Keep the exact edit cursor and selection when the mapped source is already on screen.
  // The range also covers folded source represented by an in-place preview.
  const auto range = vte::TextEditUtils::visibleBlockRange(p_editor->getTextEdit());
  if (p_lineNumber < range.first || p_lineNumber > range.second) {
    p_editor->scrollToLine(p_lineNumber, false);
  }
}

void MarkdownViewWindow2::setModeInternal(ViewWindowMode p_mode, bool p_syncBuffer) {
  if (m_readerLifecycleClosing) {
    return;
  }
  const QPointer<MarkdownViewWindow2> guard(this);
  const auto generation = getBufferGeneration();
  cancelEditorImageView();
  if (p_mode == m_mode) {
    if (isVisible() && m_readerResourcesSuspended) {
      resumeInactiveReader();
    }
    return;
  }

  // Reentrancy guard: processEvents() below can trigger signals that
  // re-enter this method. Skip if already switching.
  if (m_switchingMode) {
    return;
  }
  if (m_protectedView) {
    m_protectedView->splitterSizesOnReady.clear();
  }
  if (!m_preparingPresentation) {
    setPresentationMode(false);
  }
  ++m_navigationGeneration;
  if (m_viewer) {
    m_viewer->invalidateNavigationTargets();
  }
  m_switchingMode = true;
  if (isReadMode()) {
    setDebugVisible(false);
  }
  updateEditSectionNumberOptions(false);

  m_outlineProvider->setReorderSupported(false);

  // When leaving Edit mode, sync editor content to buffer immediately
  // so the buffer has the latest content for the viewer to read.
  if (m_mode == ViewWindowMode::Edit && m_editor && p_syncBuffer && m_sourceAvailable) {
    auto *bufferService = getServices().get<BufferService>();
    if (bufferService && !bufferService->syncNow(getBuffer().id())) {
      m_switchingMode = false;
      updateEditSectionNumberOptions(true);
      showMessage(tr("Unable to read the editor content"));
      return;
    }
    // A delayed reader focus gain must not mistake our own sync for an external edit.
    m_lastKnownRevision = getBuffer().getRevision();
  }

  m_previousMode = m_mode;
  m_mode = p_mode;

  // Disable propagation during mode switch to avoid false dirty marking.
  m_propagateEditorToBuffer = false;

  const auto desiredEditViewMode = m_editor ? m_editViewMode
                                            : getServices()
                                                  .get<ConfigMgr2>()
                                                  ->getEditorConfig()
                                                  .getMarkdownEditorConfig()
                                                  .getEditViewMode();
  auto transition = MarkdownViewWindowController::computeModeTransition(
      static_cast<int>(m_previousMode), static_cast<int>(m_mode), desiredEditViewMode,
      m_editor != nullptr, m_viewer != nullptr, p_syncBuffer);

  // Diagnostics: the two preview pipelines (read-mode render and edit-mode
  // in-place preview) share one JS main thread, and their overlap is invisible
  // without a timestamp on each boundary. See
  // .kilo/plans/1788310000000-trace-edit-mode-preview-perf.md.
  qCDebug(lcPerfPreview) << "setModeInternal prevMode=" << static_cast<int>(m_previousMode)
                         << "mode=" << static_cast<int>(m_mode)
                         << "setupViewer=" << transition.needSetupViewer
                         << "setupEditor=" << transition.needSetupEditor
                         << "syncEditorFromBuffer=" << transition.syncEditorFromBuffer
                         << "syncViewerFromBuffer=" << transition.syncViewerFromBuffer
                         << "atMs=" << QDateTime::currentMSecsSinceEpoch();

  // An unavailable preview must not prevent editing the document.
  bool previewUnavailable = false;
  if (m_sourceAvailable && !m_readerResourcesSuspended && transition.needSetupViewer &&
      !setupViewer()) {
    if (m_mode == ViewWindowMode::Read && !m_rebindingBuffer) {
      m_mode = m_previousMode;
      m_propagateEditorToBuffer = m_mode == ViewWindowMode::Edit && m_editor;
      m_switchingMode = false;
      updateEditSectionNumberOptions(true);
      return;
    }
    previewUnavailable = true;
    transition.syncViewerFromBuffer = false;
  }
  if (transition.needSetupEditor) {
    setupTextEditor();
  }

  updateSectionNumberOptions();

  // Sync content from buffer to the target view.
  if (transition.syncEditorFromBuffer) {
    syncTextEditorFromBuffer(transition.syncPositionFromPrevMode);
  }
  if (transition.syncViewerFromBuffer ||
      (p_syncBuffer && m_mode == ViewWindowMode::Read && m_viewer && !m_viewerPageInitialized)) {
    syncViewerFromBuffer(transition.syncPositionFromPrevMode);
  }

  // The initial buffer read above lazily loaded the content and bumped the
  // vxcore revision past what ViewWindow2's constructor captured. Adopt it now,
  // before the show/focus/processEvents() sequence below delivers a focus event:
  // onFocusGained() would otherwise read the stale revision as an external
  // modification and re-sync -- a full viewer page reload that discards any
  // render pass already running in the page.
  if (transition.adoptInitialRevision) {
    m_lastKnownRevision = getBuffer().isValid() ? getBuffer().getRevision() : 0;
  }

  // Show/hide widgets and set focus.
  switch (m_mode) {
  case ViewWindowMode::Read:
    if (m_viewer) {
      m_viewer->setVisible(m_sourceAvailable && (!m_rebindingBuffer || m_viewerReady));
      m_viewer->setFocus();
    }
    if (m_editor) {
      m_editor->hide();
      // Hide splitter handle to avoid visible border in readable-width mode.
      if (m_splitter->count() > 1) {
        m_splitter->handle(1)->setVisible(false);
      }
    }
    if (m_legacyImageBar) {
      m_legacyImageBar->hide();
    }
    // Clear in-flight messages on mode changes, then hide the overlay until
    // the next transient message. Neither showing nor hiding it resizes content.
    showMessage(QString());
    if (statusBar()) {
      statusBar()->hide();
    }
    break;

  case ViewWindowMode::Edit:
    m_editor->setVisible(m_sourceAvailable);
    m_editor->setFocus();
    if (m_legacyImageBar) {
      m_legacyImageBar->show();
    }
    setEditViewMode(previewUnavailable ? MarkdownEditorConfig::EditViewMode::EditOnly
                                       : desiredEditViewMode);
    // Clear any transient message left over from Read mode (e.g. a hovered link
    // URL) so we don't carry it into Edit's editor status surface.
    showMessage(QString());
    if (statusBar()) {
      statusBar()->show();
    }
    break;

  default:
    Q_ASSERT(false);
    break;
  }

  // Let widgets show before scrolling (processEvents needed for geometry).
  QCoreApplication::processEvents();
  if (!guard || generation != getBufferGeneration()) {
    return;
  }

  // Post-switch: sync buffer content to active view (content-only, no template reload).
  // Uses revision check to skip if already synced during initial setup.
  if (m_sourceAvailable && transition.syncBufferToActiveView) {
    auto bufferRevision = getBuffer().isValid() ? getBuffer().getRevision() : 0;
    switch (m_mode) {
    case ViewWindowMode::Read:
      if (m_viewer && m_viewerBufferRevision != bufferRevision) {
        auto state = MarkdownEditorController::prepareBufferState(getBuffer());
        if (state.valid) {
          int lineNumber = transition.syncPositionFromPrevMode ? getEditLineNumber() : -1;
          requestNativeRender(state.content, lineNumber);
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
        syncEditorPositionFromReadMode(m_editor, getReadLineNumber());
      }
      break;
    default:
      break;
    }
  }

  m_switchingMode = false;
  if (isVisible() && m_readerResourcesSuspended) {
    resumeInactiveReader();
  }
  if (previewUnavailable) {
    showMessage(isReadMode() ? tr("Unable to open preview")
                             : tr("Unable to open preview; the editor remains available"));
  }

  // Enable editor-to-buffer propagation now that initial sync is done.
  // This must happen after all sync calls to avoid marking the buffer dirty
  // from the initial content load.
  if (m_mode == ViewWindowMode::Edit && m_editor && m_sourceAvailable) {
    m_propagateEditorToBuffer = true;

    // Republish the existing editor snapshot when returning from Read mode;
    // unchanged content does not necessarily emit headingsChanged again.
    const auto &editorHeadings = m_editor->getHeadings();
    auto outline = headingsToOutline(editorHeadings);
    outline->m_hasSectionNumber = m_editor->getHeadingsHaveSectionNumber();
    for (int i = 0; i < editorHeadings.size(); ++i) {
      outline->m_headings[i].m_reorderable =
          editorHeadings[i].m_startPos >= 0 &&
          editorHeadings[i].m_endPos > editorHeadings[i].m_startPos;
    }
    const auto &buffer = getBuffer();
    outline->m_reorderSupported =
        !m_editor->isReadOnly() && buffer.isValid() && !buffer.isReadOnly();
    m_outlineProvider->setOutline(outline);
  }

  updateEditSectionNumberOptions(true);
  emit modeChanged();

  // Legacy image-folder check: deferred off this call stack, at most once per
  // bound document. Detection is itself a cmark parse, so it IS "after the markdown
  // parse"; a highlighter signal would only add latency without removing it.
  if (m_mode == ViewWindowMode::Edit) {
    scheduleLegacyImageCheck();
  }

  // Update find-and-replace replace-enabled state on mode switch.
  if (findAndReplaceWidgetVisible()) {
    setFindAndReplaceReplaceEnabled(m_sourceAvailable && !isReadMode());
  }
}

// ============ Sync Paths ============

// Path 1: Buffer -> Editor.
void MarkdownViewWindow2::syncTextEditorFromBuffer(bool p_syncPositionFromReadMode) {
  if (!m_editor) {
    return;
  }

  updateEditSectionNumberOptions(false);
  const bool old = m_propagateEditorToBuffer;
  m_propagateEditorToBuffer = false;

  auto state = m_sourceAvailable ? MarkdownEditorController::prepareBufferState(getBuffer())
                                 : MarkdownEditorController::BufferState();
  m_sourceAvailable = state.valid;
  if (m_sourceAvailable) {
    m_editor->setReadOnly(state.readOnly);
    m_editor->setContentPath(state.basePath);
    m_editor->setBasePath(state.basePath);
    // Saving our own text may advance the buffer revision without changing the document.
    // Avoid resetting the cursor, selection and undo history in that case.
    const bool contentChanged =
        m_resetTextEditorForNewBuffer || m_editor->getText() != state.content;
    if (contentChanged) {
      m_editor->setText(state.content);
    }
    if (m_resetTextEditorForNewBuffer) {
      auto *edit = m_editor->getTextEdit();
      edit->setTextCursor(QTextCursor(edit->document()));
      edit->verticalScrollBar()->setValue(0);
      edit->horizontalScrollBar()->setValue(0);
      TextViewWindowHelper::clearSearchHighlights(this);
    }
    m_editor->setModified(state.modified);

    if (p_syncPositionFromReadMode) {
      if (contentChanged) {
        m_editor->scrollToLine(getReadLineNumber(), false);
      } else {
        syncEditorPositionFromReadMode(m_editor, getReadLineNumber());
      }
    }
  } else {
    m_editor->setReadOnly(true);
    m_editor->setContentPath(QString());
    m_editor->setBasePath(QString());
    m_editor->setText(QString());
    m_editor->setModified(false);
  }

  if (m_resetTextEditorForNewBuffer) {
    m_editor->getTextEdit()->document()->clearUndoRedoStacks();
  }
  m_resetTextEditorForNewBuffer = false;
  m_textEditorBufferRevision = state.revision;
  m_propagateEditorToBuffer = old;
  updateEditSectionNumberOptions(true);
}

void MarkdownViewWindow2::initializeViewerPage() {
  Q_ASSERT(m_viewer);
  invalidateNativeRender();
  m_viewerReady = false;
  m_viewerPageInitialized = true;
  m_viewerBufferRevision = -1;
  m_viewer->jobSession()->invalidate();
  adapter()->reset();
  m_viewer->jobSession()->setProperty("markdownJobBufferId", getBuffer().id());
  m_viewer->jobSession()->setProperty("markdownJobProtected", getBuffer().isEncrypted());
  m_webJobs->setPreferredSession(m_viewer->jobSession(), m_protectedView
                                                             ? m_protectedView->highlightStyleSheet
                                                             : QByteArray());
  if (m_protectedView) {
    m_viewer->load(m_protectedView->page.url);
  } else {
    const auto html = getServices().get<HtmlTemplateService>()->getMarkdownViewerTemplate();
    m_viewer->setHtml(html, PathUtils::pathToUrl(getBuffer().resolvedPath()));
  }
}

MarkdownHtmlOptions MarkdownViewWindow2::nativeRenderOptions() const {
  MarkdownHtmlOptions options;
  if (getBuffer().isEncrypted()) {
    // Preserve the protected template's fixed grammar, including soft newlines,
    // independently of ordinary preferences and the native renderer's defaults.
    options.htmlEnabled = true;
    options.autoBreaks = false;
    options.linkify = true;
  } else {
    const auto &config =
        getServices().get<ConfigMgr2>()->getEditorConfig().getMarkdownEditorConfig();
    options.htmlEnabled = config.getHtmlTagEnabled();
    options.autoBreaks = config.getAutoBreakEnabled();
    options.linkify = config.getLinkifyEnabled();
  }
  return options;
}

void MarkdownViewWindow2::updateNativeDocumentRetention() {
  if (!m_editor) {
    return;
  }
  auto *highlighter = m_editor->getHighlighter();
  highlighter->setDocumentParseOptions(MarkdownHtmlRenderer::parseOptions(nativeRenderOptions()));
  const bool needed = m_viewer && m_sourceAvailable && !m_rebindingBuffer &&
                      !m_readerResourcesSuspended && !m_readerLifecycleClosing &&
                      !m_nativeLocking &&
                      (isReadMode() || m_printPending ||
                       m_editViewMode == MarkdownEditorConfig::EditViewMode::EditPreview);
  highlighter->setDocumentRetentionEnabled(needed);
}

void MarkdownViewWindow2::invalidateNativeRender(bool p_suspend) {
  m_waitingNativeDocument = false;
  m_nativeRenderToken.clear();
  m_renderBufferGeneration = 0;
  m_renderEditorGeneration = 0;
  m_renderBufferRevision = -1;
  m_nativeRenderLineNumber = -1;
  m_renderUsesEditor = false;
  if (m_nativeRender) {
    if (p_suspend) {
      m_nativeRender->setSuspended(true);
    } else {
      m_nativeRender->invalidate();
    }
  }
  if (p_suspend && m_editor) {
    m_editor->getHighlighter()->setDocumentRetentionEnabled(false);
  }
}

bool MarkdownViewWindow2::nativeRenderSourceIsCurrent() const {
  if (m_rebindingBuffer || !m_sourceAvailable || !getBuffer().isValid() ||
      m_renderBufferGeneration != getBufferGeneration()) {
    return false;
  }
  if (m_renderUsesEditor) {
    return m_editor && m_editor->getHighlighter()->documentGeneration() == m_renderEditorGeneration;
  }
  return getBuffer().getRevision() == m_renderBufferRevision;
}

void MarkdownViewWindow2::requestNativeRender(const QString &p_text, int p_lineNumber) {
  if (!m_viewer || m_rebindingBuffer || !m_sourceAvailable || m_readerResourcesSuspended ||
      m_readerLifecycleClosing || m_nativeLocking) {
    return;
  }
  if (m_editor && p_text == m_editor->getText()) {
    requestEditorNativeRender(p_lineNumber);
    return;
  }
  m_waitingNativeDocument = false;
  m_nativeRenderLineNumber = p_lineNumber;
  m_renderBufferGeneration = getBufferGeneration();
  m_renderBufferRevision = getBuffer().getRevision();
  m_renderUsesEditor = false;
  m_nativeRenderToken = m_nativeRender->render(p_text.toUtf8(), nativeRenderOptions());
}

void MarkdownViewWindow2::requestEditorNativeRender(int p_lineNumber) {
  if (!m_editor || !m_viewer || m_rebindingBuffer || !m_sourceAvailable ||
      m_readerResourcesSuspended || m_readerLifecycleClosing || m_nativeLocking) {
    return;
  }
  m_waitingNativeDocument = false;
  m_nativeRenderLineNumber = p_lineNumber;
  m_renderBufferGeneration = getBufferGeneration();
  m_renderBufferRevision = getBuffer().getRevision();
  m_renderUsesEditor = true;
  auto *highlighter = m_editor->getHighlighter();
  highlighter->setDocumentParseOptions(MarkdownHtmlRenderer::parseOptions(nativeRenderOptions()));
  highlighter->setDocumentRetentionEnabled(true);
  m_renderEditorGeneration = highlighter->documentGeneration();
  const auto document = highlighter->currentDocument();
  if (document) {
    // The full parse already owns canonical UTF-8; do not serialize the editor again.
    renderNativeDocument(document);
  } else if (highlighter->hasCurrentDocumentParse()) {
    m_nativeRender->invalidate();
    m_nativeRenderToken.clear();
    m_waitingNativeDocument = true;
    highlighter->requestDocument();
  } else {
    // Metadata may already be current while EditOnly discarded the tree. Parse
    // only for rendering, rather than redoing editor projections/highlighting.
    m_nativeRenderToken = m_nativeRender->render(
        m_editor->getTextEdit()->document()->toPlainText().toUtf8(), nativeRenderOptions());
  }
}

void MarkdownViewWindow2::renderNativeDocument(
    const QSharedPointer<const vte::MarkdownDocument> &p_document) {
  if (!p_document || !nativeRenderSourceIsCurrent() || !m_viewer || m_readerResourcesSuspended ||
      m_readerLifecycleClosing || m_nativeLocking) {
    return;
  }
  m_nativeRenderToken =
      m_nativeRender->render(p_document->source(), nativeRenderOptions(), p_document);
}

void MarkdownViewWindow2::onNativeProtectedLockingChanged(bool p_locking) {
  if (!getBuffer().isEncrypted() || m_readerLifecycleClosing) {
    return;
  }
  m_nativeLocking = p_locking;
  invalidateNativeRender();
  updateNativeDocumentRetention();
  if (!p_locking && isEnabled() && !m_readerResourcesSuspended && m_viewer) {
    syncViewerFromBuffer(false);
  }
}

// Path 2: Buffer -> Viewer. A live editor remains authoritative during refresh.
void MarkdownViewWindow2::syncViewerFromBuffer(bool p_syncPositionFromEditMode) {
  cancelEditorImageView();
  if (!m_preparingPresentation) {
    setPresentationMode(false);
  }
  if (!m_viewer) {
    return;
  }
  m_pendingAnchor.clear();
  const auto state = m_sourceAvailable ? MarkdownEditorController::prepareBufferState(getBuffer())
                                       : MarkdownEditorController::BufferState();
  if (!state.valid) {
    m_sourceAvailable = false;
    invalidateNativeRender(true);
    m_viewerReady = false;
    m_viewerPageInitialized = false;
    adapter()->reset();
    m_viewer->hide();
    showMessage(tr("Unable to load note content"));
    return;
  }
  if (!m_viewerPageInitialized) {
    initializeViewerPage();
  }
  const bool useDraft = m_editor && m_mode == ViewWindowMode::Edit;
  const auto content = useDraft ? m_editor->getText() : state.content;
  const bool matchesBuffer = content == state.content;
  const int line = !m_restoreReaderStatePending && p_syncPositionFromEditMode && m_editor
                       ? m_editor->getTopLine()
                       : -1;
  if (useDraft) {
    requestEditorNativeRender(line);
  } else {
    requestNativeRender(content, line);
  }
  m_viewerBufferRevision = matchesBuffer ? state.revision : -1;
}

// Path 4: Editor -> Viewer (debounced preview).
void MarkdownViewWindow2::syncEditorContentsToPreview() {
  if (m_editorImageRequest) {
    m_deferredPreviewUpdate = true;
    return;
  }
  if (!m_sourceAvailable || m_readerResourcesSuspended || !m_viewerReady || isReadMode() ||
      m_editViewMode == MarkdownEditorConfig::EditViewMode::EditOnly) {
    return;
  }
  // NOTE: Do NOT update m_viewerBufferRevision here.
  // This is visual preview sync only, not a buffer sync event.
  m_deferredPreviewUpdate = false;
  requestEditorNativeRender(m_restoreReaderStatePending ? -1 : m_editor->getTopLine());
}

void MarkdownViewWindow2::syncEditorPositionToPreview() {
  if (m_restoreReaderStatePending || !m_viewerReady || isReadMode() ||
      m_editViewMode == MarkdownEditorConfig::EditViewMode::EditOnly) {
    return;
  }
  adapter()->scrollToPosition(MarkdownViewerAdapter::Position(m_editor->getTopLine(), QString()));
}

// ViewWindow2 pure virtual override.
void MarkdownViewWindow2::syncEditorFromBuffer() {
  cancelEditorImageView();
  setPresentationMode(false);
  VxCoreError error = VXCORE_OK;
  getBuffer().peekContentRaw(&error);
  m_sourceAvailable = error == VXCORE_OK && !getBuffer().getBuffer().isEmpty();
  syncTextEditorFromBuffer(false);
  if (m_sourceAvailable && !m_rebindingBuffer && !m_readerResourcesSuspended) {
    m_nativeRender->setSuspended(false);
  }
  if (m_sourceAvailable && !m_rebindingBuffer && !m_readerResourcesSuspended && !m_viewer &&
      (isReadMode() || m_editViewMode == MarkdownEditorConfig::EditViewMode::EditPreview)) {
    setupViewer();
  }
  syncViewerFromBuffer(false);
  m_lastKnownRevision = getBuffer().isValid() ? getBuffer().getRevision() : 0;
  if (!m_rebindingBuffer && m_sourceAvailable) {
    if (m_editor) {
      m_editor->setVisible(!isReadMode());
      m_editor->setInplacePreviewEnabled(m_inplacePreviewEnabled);
    }
    if (!m_readerResourcesSuspended) {
      m_webJobs->setSuspended(false);
      refreshEditorWebJobs();
    }
  }
}

// ============ Content ============

QString MarkdownViewWindow2::getLatestContent() const {
  if (!m_sourceAvailable) {
    throw std::runtime_error("The Markdown source is unavailable");
  }
  if (m_editor) {
    return m_editor->getText();
  }
  // In read mode, m_editor is null. Fall back to buffer content.
  VxCoreError error = VXCORE_OK;
  const auto content = getBuffer().peekContentRaw(&error);
  if (error != VXCORE_OK) {
    throw std::runtime_error("The Markdown source is unavailable");
  }
  return getBuffer().decode(content);
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

void MarkdownViewWindow2::updateSectionNumberOptions() {
  auto *configMgr = getServices().get<ConfigMgr2>();
  if (!m_viewer || !configMgr) {
    return;
  }
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mdConfig = editorConfig.getMarkdownEditorConfig();
  adapter()->setSectionNumberOptions(m_outlineProvider->getAutoSectionNumberAllowed() &&
                                         isReadMode() && mdConfig.getAutoSectionNumberEnabled(),
                                     editorConfig.getSectionNumberPattern(),
                                     editorConfig.getDetectHeading1ForSectionNumber());
}

void MarkdownViewWindow2::updateEditSectionNumberOptions(bool p_activate) {
  if (!m_editor) {
    return;
  }
  m_editor->setHeadingSectionNumberingActive(false);
  auto *configMgr = getServices().get<ConfigMgr2>();
  if (!configMgr) {
    m_editor->setHeadingSectionNumberProvider({});
    m_editSectionNumberEnabled = false;
    return;
  }
  const auto &editorConfig = configMgr->getEditorConfig();
  const bool enabled =
      m_outlineProvider->getAutoSectionNumberAllowed() &&
      editorConfig.getMarkdownEditorConfig().getAutoSectionNumberInEditModeEnabled();
  const auto pattern = SectionNumberUtils::normalizePattern(editorConfig.getSectionNumberPattern());
  const bool detectHeading1ForSectionNumber = editorConfig.getDetectHeading1ForSectionNumber();
  if (enabled != m_editSectionNumberEnabled || pattern != m_editSectionNumberPattern ||
      detectHeading1ForSectionNumber != m_editDetectHeading1ForSectionNumber) {
    m_editSectionNumberEnabled = enabled;
    m_editSectionNumberPattern = pattern;
    m_editDetectHeading1ForSectionNumber = detectHeading1ForSectionNumber;
    if (enabled) {
      m_editor->setHeadingSectionNumberProvider(
          [pattern,
           detectHeading1ForSectionNumber](const QVector<vte::md::HeadingInfo> &p_headings) {
            return MarkdownEditorController::generateSectionNumbers(p_headings, pattern,
                                                                    detectHeading1ForSectionNumber);
          });
    } else {
      m_editor->setHeadingSectionNumberProvider({});
    }
  }
  const auto &buffer = getBuffer();
  m_editor->setHeadingSectionNumberingActive(
      p_activate && m_mode == ViewWindowMode::Edit && m_propagateEditorToBuffer &&
      !m_switchingMode && buffer.isValid() && !buffer.isReadOnly() && !m_editor->isReadOnly());
}

void MarkdownViewWindow2::removeSessionHeadingSectionNumbers() {
  // Without an editor, this window has no session-owned source prefixes.
  if (!m_editor) {
    return;
  }
  const auto unavailable = [this]() {
    showMessage(tr("Section numbers could not be restored now. Auto section numbering remains off. "
                   "Make this note current and editable, then turn Allow Auto Section Number on "
                   "and off to retry."));
  };
  auto &buffer = getBuffer();
  auto *bufferService = getServices().get<BufferService>();
  const bool readMode = m_mode == ViewWindowMode::Read;
  if (!buffer.isValid() || buffer.isReadOnly() || !isEnabled() || isNoteConversionFrozen() ||
      !m_editor->isEnabled() || m_editor->isReadOnly() || m_switchingMode ||
      m_removingHeadingSectionNumbers || !bufferService ||
      bufferService->isContentReplacementActive(buffer.id()) ||
      (buffer.isEncrypted() && bufferService->isProtectedLocking()) ||
      (readMode ? m_propagateEditorToBuffer || !adapter()
                : m_mode != ViewWindowMode::Edit || !m_propagateEditorToBuffer)) {
    unavailable();
    return;
  }
  QScopedValueRollback<bool> removing(m_removingHeadingSectionNumbers, true);

  // A different split may still own unsaved text. Never replace its writer with
  // this document until both the writer and any external revision agree.
  QString writerText;
  if (!bufferService->captureActiveWriterContent(buffer.id(), &writerText) ||
      writerText != m_editor->getText()) {
    unavailable();
    return;
  }
  if (buffer.getRevision() != m_lastKnownRevision) {
    VxCoreError error = VXCORE_OK;
    const auto raw = buffer.peekContentRaw(&error);
    const auto encoded = bufferService->encodeContent(buffer.id(), writerText);
    if (error != VXCORE_OK || raw != QByteArrayView(encoded)) {
      unavailable();
      return;
    }
  }
  bufferService->registerActiveWriter(buffer.id(), reinterpret_cast<quintptr>(this),
                                      [this]() { return getLatestContent(); });

  using RemovalResult = vte::VMarkdownEditor::HeadingSectionNumberRemovalResult;
  const auto result = m_editor->removeHeadingSectionNumbers();
  if (result == RemovalResult::Unavailable) {
    unavailable();
    return;
  }
  if (result == RemovalResult::Unchanged || !readMode) {
    // Edit mode uses the ordinary contentsChanged dirty/preview path.
    return;
  }

  // Read mode keeps the same hidden document and undo stack authoritative.
  // Commit without reloading either the editor or the viewer page.
  const auto text = m_editor->getText();
  if (!buffer.setContentRaw(bufferService->encodeContent(buffer.id(), text))) {
    m_editor->getTextEdit()->document()->undo();
    unavailable();
    return;
  }
  onEditorContentsChanged();
  const int revision = buffer.getRevision();
  m_textEditorBufferRevision = revision;
  m_lastKnownRevision = revision;
  requestEditorNativeRender(adapter()->getTopLineNumber());
  m_viewerBufferRevision = revision;
}

void MarkdownViewWindow2::handleEditorConfigChange() {
  // Accepted close has retired the editor and helper, even while this window
  // remains alive for queued application-wide configuration notifications.
  if (m_readerLifecycleClosing) {
    return;
  }
  cancelEditorImageView();
  setPresentationMode(false);
  updateEditSectionNumberOptions(false);
  updateSectionNumberOptions();
  // Always update layout mode (WidgetConfig changes don't affect editor config revision).
  ViewWindow2::handleEditorConfigChange();
  updateInactiveReaderTimer();

  if (!m_editorController->checkAndUpdateConfigRevision()) {
    updateEditSectionNumberOptions(true);
    return;
  }

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mdConfig = editorConfig.getMarkdownEditorConfig();

  invalidateWebJobs();

  updatePreviewHelperFromConfig(mdConfig);

  // Update HTML template.
  auto *htmlTemplateService = getServices().get<HtmlTemplateService>();
  auto *themeService = getServices().get<ThemeService>();
  if (!getBuffer().isEncrypted()) {
    htmlTemplateService->updateMarkdownViewerTemplate(
        mdConfig, themeService->fetchWebStyleSheet(),
        themeService->getFile(Theme::File::HighlightStyleSheet));
  }

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

    config->m_documentParseOptions = MarkdownHtmlRenderer::parseOptions(nativeRenderOptions());

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

  if (m_viewer) {
    if (m_protectedView) {
      reloadProtectedViewer();
    } else {
      m_viewerPageInitialized = false;
      syncViewerFromBuffer(m_mode == ViewWindowMode::Edit);
    }
    updateWebViewerConfig();
  }
  if (m_mode == ViewWindowMode::Edit && m_editor) {
    if (m_readerResourcesSuspended) {
      m_editViewMode = mdConfig.getEditViewMode();
    } else {
      setEditViewMode(mdConfig.getEditViewMode());
    }
  }
  refreshEditorWebJobs();
  updateEditSectionNumberOptions(true);
}

void MarkdownViewWindow2::handleThemeChanged() {
  if (m_readerLifecycleClosing) {
    return;
  }
  cancelEditorImageView();
  invalidateWebJobs();
  setPresentationMode(false);
  updateEditSectionNumberOptions(false);
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

    config->m_documentParseOptions = MarkdownHtmlRenderer::parseOptions(nativeRenderOptions());

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
    if (!getBuffer().isEncrypted()) {
      htmlTemplateService->updateMarkdownViewerTemplate(
          mdConfig, themeService->fetchWebStyleSheet(),
          themeService->getFile(Theme::File::HighlightStyleSheet),
          /*p_force=*/true);
    }

    // Update WebEngine page background.
    m_viewer->page()->setBackgroundColor(themeService->getBaseBackground());

    // Reload viewer with new template (preserves buffer content, does not sync scroll from editor).
    if (m_protectedView) {
      reloadProtectedViewer();
    } else {
      m_viewerPageInitialized = false;
      syncViewerFromBuffer(false);
    }

    // Re-apply readable width colors if in readable-width mode.
    applyReadableWidth();
  }

  if (m_debugViewer) {
    m_debugViewer->page()->setBackgroundColor(themeService->getBaseBackground());
  }

  refreshEditorWebJobs();
  updateEditSectionNumberOptions(true);
}

void MarkdownViewWindow2::applyReadableWidth() {
  if (isViewFullScreen()) {
    ViewWindow2::applyReadableWidth();
    return;
  }
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
  m_editor->zoom(snapshot.zoomDelta);
  m_previewHelper->editorZoomChanged();
  vte::Key leaderKey(snapshot.shortcutLeaderKey);
  m_editor->setLeaderKeyToSkip(leaderKey.m_key, leaderKey.m_modifiers);
}

void MarkdownViewWindow2::updatePreviewHelperFromConfig(const MarkdownEditorConfig &p_mdConfig) {
  auto phConfig = MarkdownEditorController::getPreviewHelperConfig(p_mdConfig);
  m_previewHelper->setWebGraphvizEnabled(phConfig.webGraphvizEnabled);
  m_previewHelper->setInplacePreviewCodeBlocksEnabled(phConfig.inplacePreviewCodeBlocksEnabled);
  m_previewHelper->setInplacePreviewMathBlocksEnabled(phConfig.inplacePreviewMathBlocksEnabled);
  if (getBuffer().isEncrypted()) {
    return;
  }

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
  m_viewer->setZoomFactor(configMgr->getSessionConfig().getMarkdownZoomFactorInReadMode());
  m_viewer->page()->runJavaScript(
      QStringLiteral("if (window.vxcore) { window.vxcore.setHeadingFoldingEnabled(%1); }")
          .arg(mdConfig.getHeadingFoldingEnabled() ? QStringLiteral("true")
                                                   : QStringLiteral("false")));
}

// ============ Edit View Mode ============

void MarkdownViewWindow2::setEditViewMode(MarkdownEditorConfig::EditViewMode p_mode) {
  Q_ASSERT(m_mode == ViewWindowMode::Edit);
  if (!m_sourceAvailable) {
    m_editViewMode = p_mode;
    if (m_viewer) {
      m_viewer->hide();
    }
    return;
  }
  if (m_readerResourcesSuspended) {
    m_editViewMode = p_mode;
    if (isVisible()) {
      resumeInactiveReader();
    }
    return;
  }
  if (m_editorImageRequest && p_mode == m_editViewMode) {
    return;
  }
  cancelEditorImageView();
  ++m_navigationGeneration;
  if (m_viewer) {
    m_viewer->invalidateNavigationTargets();
  }
  bool modeChanged = false;
  if (m_editViewMode != p_mode) {
    m_editViewMode = p_mode;
    modeChanged = true;
    if (m_protectedView) {
      m_protectedView->splitterSizesOnReady.clear();
    }
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
    if (m_viewer) {
      m_viewer->hide();
    }
    if (m_syncPreviewTimer) {
      m_syncPreviewTimer->stop();
    }
    if (m_syncPreviewTimer) {
      disconnect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, m_syncPreviewTimer,
                 QOverload<>::of(&QTimer::start));
    }
    disconnect(m_editor, &MarkdownEditor::topLineChanged, this,
               &MarkdownViewWindow2::syncEditorPositionToPreview);
    invalidateNativeRender();
    updateNativeDocumentRetention();
    break;

  case MarkdownEditorConfig::EditViewMode::EditPreview:
    if (!m_viewer && !setupViewer()) {
      if (!m_restoreReaderStatePending) {
        m_editViewMode = MarkdownEditorConfig::EditViewMode::EditOnly;
      }
      return;
    }
    if (!m_viewerPageInitialized) {
      syncViewerFromBuffer(true);
    }
    // Restore splitter handle for draggable split.
    if (m_splitter->count() > 1) {
      m_splitter->handle(1)->setVisible(true);
    }
    m_viewer->setVisible(!m_rebindingBuffer || m_viewerReady);
    if (modeChanged) {
      WidgetUtils::distributeWidgetsOfSplitter(m_splitter);
    }
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
    updateNativeDocumentRetention();
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
      if (isVisible() && m_readerResourcesSuspended) {
        resumeInactiveReader();
      }
      if (auto *a = adapter()) {
        a->findText(QStringList(p_text), p_options);
      } else {
        showMessage(tr("Unable to load preview"));
      }
    }
  } else {
    TextViewWindowHelper::handleFindTextChanged(this, p_text, p_options);
  }
}

void MarkdownViewWindow2::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  if (isReadMode()) {
    if (isVisible() && m_readerResourcesSuspended) {
      resumeInactiveReader();
    }
    if (auto *a = adapter()) {
      a->findText(p_texts, p_options);
    } else {
      showMessage(tr("Unable to load preview"));
    }
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
    if (auto *a = adapter()) {
      a->findText(QStringList(), FindOption::FindNone);
    }
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
  if (m_editorImageOpened && m_viewer) {
    m_viewer->setFocus();
    return;
  }
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
  if (!m_sourceAvailable || m_rebindingBuffer) {
    showMessage(tr("Unable to load note content"));
    return;
  }
  auto text = selectedText();
  if (!text.isEmpty()) {
    auto info = WordCountPanel::calculateWordCount(text);
    info.m_isSelection = true;
    p_callback(info);
    return;
  }

  switch (m_mode) {
  case ViewWindowMode::Read: {
    if (!m_viewer || !adapter()->isDocumentReady() ||
        !m_nativeRender->isCurrent(adapter()->documentGeneration()) ||
        !nativeRenderSourceIsCurrent()) {
      showMessage(tr("Unable to load preview"));
      return;
    }
    const QPointer<const MarkdownViewWindow2> self(this);
    const auto bufferGeneration = getBufferGeneration();
    const auto generation = adapter()->documentGeneration();
    m_viewer->saveContent([self, bufferGeneration, generation,
                           p_callback](const QString &p_content) {
      if (!self || bufferGeneration != self->getBufferGeneration() || self->m_rebindingBuffer) {
        return;
      }
      if (p_content.isNull() || !self->m_viewer ||
          self->adapter()->documentGeneration() != generation ||
          !self->m_nativeRender->isCurrent(generation) || !self->nativeRenderSourceIsCurrent()) {
        self->showMessage(tr("Unable to load preview"));
        return;
      }
      p_callback(WordCountPanel::calculateWordCount(p_content));
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
    if (auto *a = adapter()) {
      a->findText(QStringList(), FindOption::FindNone);
    }
  } else {
    TextViewWindowHelper::clearSearchHighlights(this);
  }
}

void MarkdownViewWindow2::applyFileOpenSettings(const FileOpenSettings &p_settings) {
  if (!m_sourceAvailable) {
    return;
  }
  if (p_settings.m_lineNumber < 0 && p_settings.m_anchor.isEmpty() &&
      p_settings.m_cursorOffset < 0 && !p_settings.m_searchHighlight.m_isValid) {
    return;
  }

  if ((isReadMode() && !m_viewer) || m_restoreReaderStatePending) {
    m_inactiveReaderFileOpenSettings = p_settings;
    if (isVisible() && m_readerResourcesSuspended) {
      resumeInactiveReader();
    }
    return;
  }
  if (!p_settings.m_anchor.isEmpty() || p_settings.m_lineNumber >= 0 ||
      (p_settings.m_searchHighlight.m_isValid &&
       p_settings.m_searchHighlight.m_currentMatchLine >= 0)) {
    m_pendingAnchor.clear();
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
  if (p_obj == this) {
    switch (p_event->type()) {
    case QEvent::Show:
      m_inactiveReaderTimer->stop();
      resumeInactiveReader();
      break;
    case QEvent::Hide:
      updateInactiveReaderTimer(true);
      break;
    case QEvent::EnabledChange:
      if (isEnabled() && isVisible()) {
        resumeInactiveReader();
      }
      updateInactiveReaderTimer();
      break;
    case QEvent::Close:
      m_inactiveReaderTimer->stop();
      break;
    default:
      break;
    }
  }
  if (p_obj == this && (p_event->type() == QEvent::Show ||
                        (p_event->type() == QEvent::EnabledChange && isEnabled()))) {
    resumeDeferredPreviewUpdate();
  }
  if (p_obj == this && (p_event->type() == QEvent::Hide || p_event->type() == QEvent::Close ||
                        (p_event->type() == QEvent::EnabledChange && !isEnabled()))) {
    cancelEditorImageView();
    if (m_printPending) {
      m_printPending = false;
      m_printer.reset();
    }
  }
  if (m_editorImageRequest &&
      (p_event->type() == QEvent::KeyPress || p_event->type() == QEvent::ShortcutOverride) &&
      static_cast<QKeyEvent *>(p_event)->key() == Qt::Key_Escape &&
      !QApplication::activePopupWidget()) {
    auto *widget = qobject_cast<QWidget *>(p_obj);
    auto *focus = QApplication::focusWidget();
    const bool belongsToView =
        (widget && (widget == this || isAncestorOf(widget)) && widget->window() == window()) ||
        (qobject_cast<QWindow *>(p_obj) && focus && (focus == this || isAncestorOf(focus)) &&
         QApplication::activeWindow() == window());
    if (belongsToView) {
      p_event->accept();
      if (p_event->type() == QEvent::KeyPress) {
        cancelEditorImageView(true);
      }
      return true;
    }
  }
  if (m_presentationRequested && !m_presentationActive) {
    // A first WebEngine view may rebuild its native ancestor, transiently hiding this
    // widget during preparation. setPresentationMode() still cancels a settled hidden view.
    if (p_obj == this &&
        ((p_event->type() == QEvent::Hide && !p_event->spontaneous() && !m_preparingPresentation) ||
         p_event->type() == QEvent::Close ||
         (p_event->type() == QEvent::EnabledChange && !isEnabled()))) {
      setPresentationMode(false);
    } else if ((p_event->type() == QEvent::KeyPress ||
                p_event->type() == QEvent::ShortcutOverride) &&
               static_cast<QKeyEvent *>(p_event)->key() == Qt::Key_Escape &&
               !QApplication::activePopupWidget()) {
      auto *widget = qobject_cast<QWidget *>(p_obj);
      auto *focus = QApplication::focusWidget();
      const bool belongsToView =
          (widget && (widget == this || isAncestorOf(widget)) && widget->window() == window()) ||
          (qobject_cast<QWindow *>(p_obj) && focus && (focus == this || isAncestorOf(focus)) &&
           QApplication::activeWindow() == window());
      if (belongsToView) {
        p_event->accept();
        if (p_event->type() == QEvent::KeyPress) {
          setPresentationMode(false);
        }
        return true;
      }
    }
  }
  if (p_obj == m_splitter) {
    if (p_event->type() == QEvent::FocusIn) {
      focusEditor();
    }
  }

  return ViewWindow2::eventFilter(p_obj, p_event);
}

void MarkdownViewWindow2::handleExternalMathHighlightRequest(int p_idx, quint64 p_timeStamp,
                                                             const QString &p_text) {
  ensureExternalHighlightStyles();
  m_webJobs->highlightMath(p_idx, p_timeStamp, p_text);
}

void MarkdownViewWindow2::ensureExternalHighlightStyles() {
  if (m_readerResourcesSuspended || m_rebindingBuffer || !m_sourceAvailable ||
      m_codeBlockStylesInitialized || m_codeBlockStylesPending || !isEnabled()) {
    return;
  }
  if (!m_editor) {
    return;
  }

  m_codeBlockStylesPending = true;
  const auto generation = ++m_highlightStyleGeneration;
  const auto bufferGeneration = getBufferGeneration();
  const QPointer<MarkdownViewWindow2> guard(this);
  m_webJobs->fetchHighlightStyles(
      [this, guard, generation,
       bufferGeneration](const QVector<MarkdownWebJobController::CssRuleStyle> *rules) {
        if (!guard || generation != m_highlightStyleGeneration ||
            bufferGeneration != getBufferGeneration() || m_rebindingBuffer) {
          return;
        }
        m_codeBlockStylesPending = false;
        if (!rules || !m_editor) {
          return;
        }
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

        // Installing the palette clears cached token formats and requests a fresh
        // highlight without replacing the document or its undo history.
        m_codeBlockStylesInitialized = true;
        m_editor->setExternalCodeBlockHighlihgtStyles(styles);
      });
}

void MarkdownViewWindow2::onPrintFinished(bool p_succeeded) {
  const bool current =
      m_printer && m_printBufferGeneration == getBufferGeneration() && !m_rebindingBuffer;
  m_printer.reset();
  if (current) {
    showMessage(p_succeeded ? tr("Printing completed") : tr("Failed to print"));
  }
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
  if (m_previousMode == ViewWindowMode::Read) {
    return adapter() ? adapter()->getTopLineNumber() : m_inactiveReaderState.m_topLineNumber;
  }
  return -1;
}

bool MarkdownViewWindow2::isReadMode() const { return m_mode == ViewWindowMode::Read; }

QStringList MarkdownViewWindow2::getAttachmentScanExcludedPaths() const {
  const auto &buffer = getBuffer();
  const auto resolved = buffer.resolvedPath();
  if (!m_sourceAvailable || m_rebindingBuffer || !buffer.isValid() || resolved.isEmpty()) {
    return {};
  }
  const auto basePath = QFileInfo(resolved).path();
  const int flags = vte::MarkdownLink::LocalRelativeInternal |
                    vte::MarkdownLink::LocalRelativeExternal | vte::MarkdownLink::LocalAbsolute;
  const auto images = vte::MarkdownUtils::fetchImageLinks(
      getLatestContent(), basePath, static_cast<vte::MarkdownLink::TypeFlags>(flags));
  QStringList paths;
  paths.reserve(images.size() + m_initialImages.size() + m_insertedImages.size());
  for (const auto &image : images) {
    paths.append(image.m_path);
  }
  const auto appendTracked = [&paths, &basePath](const QSet<QString> &p_images) {
    for (const auto &url : p_images) {
      const auto path = vte::MarkdownUtils::linkUrlToPath(basePath, url);
      const QFileInfo info(path);
      if (!path.isEmpty() && info.isAbsolute() && info.isNativePath()) {
        paths.append(path);
      }
    }
  };
  appendTracked(m_initialImages);
  appendTracked(m_insertedImages);
  return paths;
}

void MarkdownViewWindow2::snapshotInitialImages() {
  if (getBuffer().isEncrypted()) {
    return;
  }
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
  // Collection, not rewriting: reference-style images have no destination span
  // and are still returned, so a reference-style image is no longer mistaken
  // for one this note stopped using.
  auto images = vte::MarkdownUtils::fetchImageLinks(
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
  if (getBuffer().isEncrypted()) {
    return;
  }
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
  const auto images = vte::MarkdownUtils::fetchImageLinks(
      content, resourcePath, static_cast<vte::MarkdownLink::TypeFlags>(linkFlags));

  QSet<QString> currentImages;
  // An HTML `src` whose value still contains an `&` after decoding holds a
  // character reference this tree does not resolve the way the RENDERER does.
  // cmark's decoder (which scanHtmlImgTags() uses) requires the terminating
  // semicolon and applies CommonMark's numeric rules; an HTML attribute parser
  // additionally accepts semicolon-less legacy names (`&copy.png`) and remaps
  // C1 numeric references. The destination the browser actually loads may
  // therefore differ from the one recorded here.
  //
  // That is harmless for preview and export -- an image simply does not
  // resolve -- but NOT for deletion. So an unresolved `&` anywhere in the note
  // disarms obsolete-image deletion entirely for this pass, exactly as the
  // raw-content net below disarms it per url. Nothing is lost by keeping an
  // asset; an asset deleted out from under a rendered image is unrecoverable.
  bool ambiguousReference = false;
  for (const auto &img : images) {
    if (!img.m_urlInLink.isEmpty()) {
      currentImages.insert(img.m_urlInLink);
    }
    if (img.m_syntax == vte::MarkdownLink::Syntax::Html &&
        img.m_urlInLink.contains(QLatin1Char('&'))) {
      ambiguousReference = true;
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

  if (ambiguousReference) {
    qWarning() << "MarkdownViewWindow2: skipped obsolete-image cleanup; a src holds an "
                  "unresolved character reference";
    obsoleteImages.clear();
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
  if (getBuffer().isEncrypted() || !m_sourceAvailable || m_legacyImageCheckDone) {
    return;
  }

  const auto generation = getBufferGeneration();
  QTimer::singleShot(0, this, [this, generation]() {
    if (generation == getBufferGeneration() && !m_rebindingBuffer) {
      runLegacyImageCheck();
    }
  });
}

void MarkdownViewWindow2::runLegacyImageCheck() {
  // Re-verify the mode FIRST. modeChanged() is synchronous and a slot can flip
  // back to Read before this timer fires; consuming the once-only check in the
  // wrong mode would disable the feature for this bound document.
  if (m_mode != ViewWindowMode::Edit || !m_editor || !m_sourceAvailable ||
      getBuffer().isEncrypted() || m_rebindingBuffer) {
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

  // One undoable edit. The rewrites are already sorted DESCENDING by urlStart,
  // which keeps every earlier offset valid — never sort ascending.
  {
    QTextCursor cur(doc);
    cur.beginEditBlock();
    for (const auto &rw : rewrites) {
      // The span, never oldUrlInLink.size(): the source spelling of a
      // destination can be longer than its resolved value, and measuring the
      // replacement with the resolved length would eat the following character.
      cur.setPosition(rw.urlStart);
      cur.setPosition(rw.urlEnd, QTextCursor::KeepAnchor);
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
  const QPointer<MarkdownViewWindow2> guard(this);
  const auto generation = getBufferGeneration();
  save();
  if (!guard || generation != getBufferGeneration()) {
    return;
  }

  if (m_legacyImageBar) {
    m_legacyImageBar->deleteLater();
    m_legacyImageBar = nullptr;
  }

  showMessage(tr("Migrated %n image(s) to the assets folder.", "", distinctDestinations.size()));
}

void MarkdownViewWindow2::onTaskListItemToggleRequested(const QString &p_generation,
                                                        int p_lineNumber, bool p_checked) {
  const auto reject = [this, p_generation, p_lineNumber](const QString &p_msg) {
    if (adapter()) {
      adapter()->emitTaskListToggleRejected(p_generation, p_lineNumber);
    }
    if (!p_msg.isEmpty()) {
      showMessage(p_msg);
    }
  };

  if (p_lineNumber < 0 || !m_sourceAvailable || m_rebindingBuffer ||
      p_generation != m_nativeRenderToken || !m_nativeRender->isCurrent(p_generation) ||
      !nativeRenderSourceIsCurrent()) {
    reject(QString());
    return;
  }

  const QScopedValueRollback<bool> applyingToggle(m_applyingTaskToggle, true);
  auto &buffer = getBuffer();
  if (!buffer.isValid()) {
    reject(QString());
    return;
  }
  if (buffer.isReadOnly()) {
    reject(tr("This note is read-only."));
    return;
  }

  auto *bufferSvc = getServices().get<BufferService>();
  if (!bufferSvc) {
    reject(QString());
    return;
  }

  if (m_editor) {
    // Hazard: getLatestContent() returns the editor text whenever the editor
    // exists, even in Read mode. The edit MUST therefore go through the editor
    // document, or the next active-writer pull reverts it.
    auto *doc = m_editor->getTextEdit()->document();
    const auto block = doc->findBlockByNumber(p_lineNumber);
    if (!block.isValid()) {
      reject(QString());
      return;
    }

    const QString newLine =
        MarkdownViewWindowController::rewriteTaskListLine(block.text(), p_checked);
    if (newLine.isNull()) {
      reject(QString());
      return;
    }

    QTextCursor cur(block);
    cur.beginEditBlock();
    cur.setPosition(block.position());
    cur.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
    cur.insertText(newLine);
    cur.endEditBlock();

    if (isReadMode()) {
      // m_propagateEditorToBuffer is false in Read mode, so push explicitly.
      // getText() (not toPlainText()) re-applies the file/platform EOL policy.
      if (!buffer.setContentRaw(bufferSvc->encodeContent(buffer.id(), m_editor->getText()))) {
        // Roll the cursor edit back. Safe: nothing has run since endEditBlock(),
        // so it is still the topmost undo command.
        doc->undo();
        reject(QString());
        return;
      }
      // markDirty + m_editorDirty + statusChanged, so the tab's modified
      // indicator updates immediately. Plain markDirty() emits nothing.
      onEditorContentsChanged();
    }
    // Edit mode: contentsChanged -> onEditorContentsChanged -> markDirty and the
    // preview debounce take over.
  } else {
    // Read mode with no editor: operate on the buffer content directly.
    const QString text = buffer.decode(buffer.getContentRaw());
    QStringList lines = text.split(QLatin1Char('\n'));
    if (p_lineNumber >= lines.size()) {
      reject(QString());
      return;
    }

    const QString newLine =
        MarkdownViewWindowController::rewriteTaskListLine(lines.at(p_lineNumber), p_checked);
    if (newLine.isNull()) {
      reject(QString());
      return;
    }

    lines[p_lineNumber] = newLine;
    if (!buffer.setContentRaw(
            bufferSvc->encodeContent(buffer.id(), lines.join(QLatin1Char('\n'))))) {
      reject(QString());
      return;
    }
    onEditorContentsChanged();
  }

  // Do NOT re-render the viewer: the DOM is already correct and a re-render
  // would reset the scroll position. setContentRaw bumped the vxcore content
  // revision, so every sync cache must be advanced to it or the next focus /
  // mode change forces a reload (destroying undo history).
  const int rev = buffer.getRevision();
  m_renderBufferRevision = rev;
  if (m_renderUsesEditor && m_editor) {
    m_renderEditorGeneration = m_editor->getHighlighter()->documentGeneration();
  }
  m_viewerBufferRevision = rev;
  m_lastKnownRevision = rev;
  if (m_editor) {
    // Never set this to -1: a forced mismatch makes the next Read->Edit switch
    // call VTextEditor::setText(), which wipes the undo stack.
    m_textEditorBufferRevision = rev;
  }
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

ViewWindow2::ViewPositionState MarkdownViewWindow2::capturePositionState() const {
  if (m_mode == ViewWindowMode::Edit && m_editor) {
    return captureEditorPositionState(m_editor->getTextEdit());
  }

  ViewPositionState s;
  if (m_mode == ViewWindowMode::Read) {
    // Read mode has no caret; only the top line is preserved.
    s.m_topLineNumber =
        adapter() ? adapter()->getTopLineNumber() : m_inactiveReaderState.m_topLineNumber;
  }
  return s;
}

void MarkdownViewWindow2::restorePositionState(const ViewPositionState &p_state) {
  if (m_mode == ViewWindowMode::Edit && m_editor) {
    applyEditorPositionState(m_editor->getTextEdit(), p_state);
  } else if (m_mode == ViewWindowMode::Read && p_state.m_topLineNumber >= 0) {
    m_inactiveReaderState.m_topLineNumber = p_state.m_topLineNumber;
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
  if (getBuffer().isEncrypted() || !m_imageHostController) {
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
