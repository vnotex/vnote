#include "typstviewwindow.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>

#include <stdexcept>

#include <vtextedit/typsteditorconfig.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/vtexteditor.h>
#include <vxcore/vxcore.h>

#include <controllers/typstviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <core/texteditorconfig.h>
#include <core/widgetconfig.h>
#include <gui/services/themeservice.h>
#include <gui/utils/widgetutils.h>

#include "editors/editorstatusbarbinder.h"
#include "encodingbutton.h"
#include "findandreplacewidget2.h"
#include "inlinebanner.h"
#include "messageboxhelper.h"
#include "outlinepopup.h"
#include "outlineprovider.h"
#include "presentationtoolbareffect.h"
#include "propertydefs.h"
#include "textviewwindowhelper.h"
#include "viewwindowtoolbarhelper2.h"
#include "zoomtoolbar.h"

using namespace vnotex;

TypstViewWindow::TypstViewWindow(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                 QWidget *p_parent, ViewWindowMode p_initialMode)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  m_controller = new TypstViewWindowController(p_services, this);
  connect(m_controller, &TypstViewWindowController::errorOccurred, this,
          [this](const QString &p_error) { showMessage(p_error); });
  connect(m_controller, &TypstViewWindowController::externalLinkConfirmationRequested, this,
          [this](const QUrl &p_url) {
            const QPointer<TypstViewWindow> guard(this);
            const QPointer<TypstViewWindowController> controller(m_controller);
            if (!m_sourceAvailable || !isEnabled()) {
              controller->openConfirmedExternalLink(QUrl());
              return;
            }
            const QUrl url(p_url);
            const int answer = MessageBoxHelper::questionYesNo(
                MessageBoxHelper::Warning,
                tr("Are you sure to open link (%1)?").arg(url.toString()),
                tr("Malicious link might do harm to your device."), QString(), window());
            if (controller) {
              const bool accepted = guard && guard->m_sourceAvailable && guard->isEnabled() &&
                                    answer == QMessageBox::Yes;
              controller->openConfirmedExternalLink(accepted ? url : QUrl());
            }
          });

  setupOutlineProvider();
  setupUI();
  // Apply the requested host mode after creating the native editor.
  setMode(p_initialMode);
  syncModeFromEditor();
}

TypstViewWindow::~TypstViewWindow() { setViewFullScreen(false); }

void TypstViewWindow::setupUI() {
  auto *toolBar = createToolBar(this);
  addToolBar(toolBar);
  addAction(toolBar, ViewWindowToolBarHelper2::EditRead);
  addLeftCommonToolBarActions(toolBar);
  addRightCommonToolBarActions(toolBar);
  updatePresentationActions();

  m_loadErrorBanner = new InlineBanner(InlineBanner::Severity::Error, QString(), this);
  addTopWidget(m_loadErrorBanner);
  m_loadErrorBanner->hide();

  m_statusBinder = new EditorStatusBarBinder(this);
  auto def = m_statusBinder->buildDef();
  StatusBarColumn encoding;
  encoding.type = StatusBarColumnType::Widget;
  const int encodingIndex = def.size();
  def << encoding;
  setStatusBarDef(def);
  statusBar()->setColumnWidget(encodingIndex, ensureEncodingButton());

  // Read collapses editor status, but transient messages can still appear like Markdown.
  connect(statusBar(), &StatusBar::messageVisibilityChanged, this, [this](bool p_visible) {
    if (m_mode == ViewWindowMode::Read) {
      statusBar()->setMaximumHeight(p_visible ? QWIDGETSIZE_MAX : 0);
    }
  });
  connect(this, &ViewWindow2::modeChanged, this, [this]() {
    showMessage(QString());
    statusBar()->setMaximumHeight(m_mode == ViewWindowMode::Read ? 0 : QWIDGETSIZE_MAX);
  });
  statusBar()->setMaximumHeight(m_mode == ViewWindowMode::Read ? 0 : QWIDGETSIZE_MAX);

  syncEditorFromBuffer();
  m_propagateEditorToBuffer = true;
  setupShortcuts();
}

void TypstViewWindow::setupPdfToolBar(QToolBar *p_toolBar) {
  const auto addPdfAction = [this, p_toolBar](const QString &p_icon, const QString &p_text) {
    auto *action =
        p_toolBar->addAction(ViewWindowToolBarHelper2::generateIcon(getServices(), p_icon), p_text);
    action->setProperty("iconName", p_icon);
    m_pdfActions.append(action);
    return action;
  };
  m_pdfActions.append(p_toolBar->addSeparator());
  m_previousPageAction =
      addPdfAction(QStringLiteral("page_previous_editor.svg"), tr("Previous Page"));
  connect(m_previousPageAction, &QAction::triggered, this,
          [this]() { m_typstEditor->goToPdfPage(m_typstEditor->currentPdfPage() - 1); });
  m_pageSpinBox = new QSpinBox(p_toolBar);
  m_pageSpinBox->setToolTip(tr("Page"));
  m_pageSpinBox->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  m_pageSpinBox->setKeyboardTracking(false);
  m_pageSpinBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  m_pdfActions.append(p_toolBar->addWidget(m_pageSpinBox));
  connect(m_pageSpinBox, &QSpinBox::editingFinished, this, [this]() {
    if (m_typstEditor && m_typstEditor->pdfPageCount() > 0) {
      m_typstEditor->goToPdfPage(m_pageSpinBox->value() - 1);
    }
  });
  m_pageCountLabel = new QLabel(p_toolBar);
  m_pageCountLabel->setProperty(PropertyDefs::c_mutedText, true);
  m_pdfActions.append(p_toolBar->addWidget(m_pageCountLabel));
  m_nextPageAction = addPdfAction(QStringLiteral("page_next_editor.svg"), tr("Next Page"));
  connect(m_nextPageAction, &QAction::triggered, this,
          [this]() { m_typstEditor->goToPdfPage(m_typstEditor->currentPdfPage() + 1); });
  m_pdfActions.append(p_toolBar->addSeparator());
  m_zoomToolBar = ViewWindowToolBarHelper2::addZoomToolBar(p_toolBar, getServices());
  connect(m_zoomToolBar, &ZoomToolBar::zoomRequested, this, [this](bool p_zoomIn) {
    if (m_typstEditor && m_typstEditor->pdfPageCount() > 0) {
      m_typstEditor->setPdfZoom(m_typstEditor->pdfZoom() * (p_zoomIn ? 1.2 : 1.0 / 1.2));
    }
  });
  connect(m_zoomToolBar, &ZoomToolBar::zoomSelected, this, &TypstViewWindow::applyPdfZoom);
}

void TypstViewWindow::addAdditionalRightToolBarActions(QToolBar *p_toolBar) {
  auto *action = addAction(p_toolBar, ViewWindowToolBarHelper2::Outline);
  auto *button = qobject_cast<QToolButton *>(p_toolBar->widgetForAction(action));
  if (button) {
    if (auto *popup = qobject_cast<OutlinePopup *>(button->menu())) {
      popup->setOutlineProvider(m_outlineProvider);
    }
  }
}

void TypstViewWindow::applyPdfZoom(qreal p_zoom) {
  if (!m_typstEditor) {
    return;
  }
  if (p_zoom == -1.0) {
    m_typstEditor->fitPdfWidth();
  } else if (p_zoom == 0.0) {
    m_typstEditor->fitPdfPage();
  } else {
    m_typstEditor->setPdfZoom(p_zoom);
  }
}

void TypstViewWindow::addAdditionalMenuActions(QMenu *p_menu) {
  p_menu->addSeparator();
  m_livePreviewAction = addAction(p_menu, ViewWindowToolBarHelper2::ToggleLivePreview);
  m_livePreviewAction->setChecked(true);
  connect(m_livePreviewAction, &QAction::toggled, this, [this](bool p_enabled) {
    if (m_typstEditor) {
      m_typstEditor->setLivePreviewEnabled(p_enabled);
    } else {
      updatePresentationActions();
    }
  });
  m_inplacePreviewAction = addAction(p_menu, ViewWindowToolBarHelper2::InplacePreview);
  m_inplacePreviewAction->setChecked(true);
  connect(m_inplacePreviewAction, &QAction::toggled, this, [this](bool p_enabled) {
    if (m_typstEditor) {
      m_typstEditor->setInplacePreviewEnabled(p_enabled);
    }
  });
}

void TypstViewWindow::updatePresentationActions() {
  if (m_typstEditor) {
    const QSignalBlocker liveBlocker(m_livePreviewAction);
    const QSignalBlocker inplaceBlocker(m_inplacePreviewAction);
    m_livePreviewAction->setChecked(m_typstEditor->isLivePreviewEnabled());
    m_inplacePreviewAction->setChecked(m_typstEditor->isInplacePreviewEnabled());
  }
  const bool pdfVisible = m_mode == ViewWindowMode::Read || m_livePreviewAction->isChecked();
  const int count = m_typstEditor ? m_typstEditor->pdfPageCount() : 0;
  const int page = m_typstEditor ? m_typstEditor->currentPdfPage() : -1;
  for (auto *action : m_pdfActions) {
    action->setVisible(pdfVisible);
    action->setEnabled(count > 0);
  }
  m_zoomToolBar->setVisible(pdfVisible);
  m_zoomToolBar->setEnabled(count > 0);
  if (m_typstEditor) {
    using ZoomMode = vte::VTypstEditor::PdfZoomMode;
    const auto mode = m_typstEditor->pdfZoomMode();
    m_zoomToolBar->setZoom(mode == ZoomMode::Width ? -1.0
                          : mode == ZoomMode::Page ? 0.0 : m_typstEditor->pdfZoom());
  }
  m_previousPageAction->setEnabled(count > 0 && page > 0);
  m_nextPageAction->setEnabled(count > 0 && page + 1 < count);
  const QSignalBlocker pageBlocker(m_pageSpinBox);
  m_pageSpinBox->setRange(count > 0 ? 1 : 0, count);
  m_pageSpinBox->setValue(count > 0 ? page + 1 : 0);
  m_pageCountLabel->setText(QStringLiteral(" / %1").arg(count));
}

void TypstViewWindow::updateFindBackend() {
  auto *find = findChild<FindAndReplaceWidget2 *>(QString(), Qt::FindDirectChildrenOnly);
  if (!find) {
    return;
  }
  const FindOptions sourceOnly =
      FindOption::CaseSensitive | FindOption::WholeWordOnly | FindOption::RegularExpression;
  const bool read = m_mode == ViewWindowMode::Read;
  if (read && !m_sourceFindOptionsSaved) {
    m_sourceFindOptions = find->getOptions() & sourceOnly;
    m_sourceFindOptionsSaved = true;
    if (m_editor) {
      TextViewWindowHelper::clearSearchHighlights(this);
    }
    find->setOptionsEnabled(sourceOnly, false);
    if (m_typstEditor && find->isVisible()) {
      m_typstEditor->setPdfSearchText(find->getFindText());
    }
  } else if (!read && m_sourceFindOptionsSaved) {
    m_sourceFindOptionsSaved = false;
    if (m_typstEditor) {
      m_typstEditor->setPdfSearchText(QString());
    }
    find->setOptionsEnabled(sourceOnly, true);
    find->setFindOptions(find->getOptions() | m_sourceFindOptions);
    if (m_sourceAvailable && find->isVisible()) {
      TextViewWindowHelper::handleFindTextChanged(this, find->getFindText(), find->getOptions());
    }
  }
  find->setReplaceEnabled(m_sourceAvailable && !read && !getBuffer().isReadOnly());
}

QSharedPointer<vte::TypstEditorConfig> TypstViewWindow::buildEditorConfig() const {
  const auto &editorConfig = getServices().get<ConfigMgr2>()->getEditorConfig();
  const auto &textEditorConfig = editorConfig.getTextEditorConfig();
  auto *themeService = getServices().get<ThemeService>();
  auto config = m_typstEditor
                    ? QSharedPointer<vte::TypstEditorConfig>::create(m_typstEditor->getConfig())
                    : QSharedPointer<vte::TypstEditorConfig>::create();
  config->m_textEditorConfig = TextViewWindowController::buildTextEditorConfigFromContent(
      editorConfig, textEditorConfig, themeService->fetchTextEditorStyle(),
      themeService->getEditorHighlightTheme(), WidgetUtils::calculateScaleFactor(),
      getEditorMaxContentWidth());
  config->m_pdfBackground = themeService->getBaseBackground();
  config->m_helperExecutable = QDir(QCoreApplication::applicationDirPath())
                                   .absoluteFilePath(QStringLiteral("vtextedit-typst"));
#ifdef Q_OS_WIN
  config->m_helperExecutable += QStringLiteral(".exe");
#endif
  config->m_allowDownloads = false;
  if (m_typstEditor) {
    config->m_initialZoom = m_typstEditor->pdfZoom();
  } else {
    const auto &project = m_controller->getProject();
    config->m_projectRoot = project.root;
    config->m_mainFile = project.mainFile;
  }
  return config;
}

void TypstViewWindow::createEditor() {
  // Called only after validating the project and reading a usable source snapshot.
  const auto &editorConfig = getServices().get<ConfigMgr2>()->getEditorConfig();
  m_controller->checkAndUpdateConfigRevision();
  m_typstEditor = new vte::VTypstEditor(buildEditorConfig(),
                                        TextViewWindowController::buildTextEditorParameters(
                                            editorConfig, editorConfig.getTextEditorConfig()),
                                        this);
  m_editor = m_typstEditor->getTextEditor();
  m_typstEditor->setReadOnly(true);
  m_typstEditor->setInplacePreviewEnabled(m_inplacePreviewAction->isChecked());
  m_typstEditor->setLivePreviewEnabled(m_livePreviewAction->isChecked());
  m_typstEditor->setMode(m_mode == ViewWindowMode::Read ? vte::VTypstEditor::Mode::Read
                                                      : vte::VTypstEditor::Mode::Edit);
  setCentralWidget(m_typstEditor);
  // Restore before observing native zoom changes; constructor/resize defaults are not choices.
  applyPdfZoom(getServices().get<ConfigMgr2>()->getSessionConfig().getTypstZoomFactor());
  connectEditorSignals();
  m_statusBinder->attach(m_editor, statusBar());
  updateEditorFromConfig();
  // Synchronize the host even when the native mode already matches its requested mode.
  syncModeFromEditor();
}

void TypstViewWindow::connectEditorSignals() {
  connect(m_editor, &vte::VTextEditor::focusIn, this, [this]() { emit focused(this); });
  connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, this, [this]() {
    if (m_propagateEditorToBuffer && m_sourceAvailable) {
      onEditorContentsChanged();
    }
  });
  connect(m_editor->getTextEdit(), &QTextEdit::cursorPositionChanged, this,
          &TypstViewWindow::updateCurrentHeading);
  connect(m_typstEditor, &vte::VTypstEditor::headingsUpdated, this,
          &TypstViewWindow::refreshOutline);
  connect(m_typstEditor, &vte::VTypstEditor::modeChanged, this,
          [this](vte::VTypstEditor::Mode) { syncModeFromEditor(); });
  connect(m_typstEditor, &vte::VTypstEditor::livePreviewEnabledChanged, this,
          &TypstViewWindow::updatePresentationActions);
  connect(m_typstEditor, &vte::VTypstEditor::inplacePreviewEnabledChanged, this,
          &TypstViewWindow::updatePresentationActions);
  connect(m_typstEditor, &vte::VTypstEditor::sourceLocationRequested, this,
          [this](const QString &p_file, int p_start, int p_end) {
            if (m_sourceAvailable && isEnabled()) {
              m_controller->requestOpenSource(p_file, p_start, p_end);
            }
          });
  connect(m_typstEditor, &vte::VTypstEditor::externalLinkRequested, this,
          [this](const QUrl &p_url) {
            if (m_sourceAvailable && isEnabled()) {
              m_controller->requestOpenLink(p_url);
            }
          });
  connect(m_typstEditor, &vte::VTypstEditor::pdfNavigationChanged, this,
          &TypstViewWindow::updatePresentationActions);
  connect(m_typstEditor, &vte::VTypstEditor::pdfZoomChanged, this,
          [this](qreal p_zoom, vte::VTypstEditor::PdfZoomMode p_mode) {
            using ZoomMode = vte::VTypstEditor::PdfZoomMode;
            const qreal choice = p_mode == ZoomMode::Width ? -1.0
                                 : p_mode == ZoomMode::Page ? 0.0 : p_zoom;
            if (m_sourceAvailable) {
              getServices().get<ConfigMgr2>()->getSessionConfig().setTypstZoomFactor(choice);
            }
            m_zoomToolBar->setZoom(choice);
          });
}

void TypstViewWindow::syncEditorFromBuffer() {
  const QScopedValueRollback<bool> guard(m_propagateEditorToBuffer, false);
  QString error;
  if (!m_controller->setBuffer(getBuffer(), &error)) {
    showSourceError(error);
    return;
  }

  VxCoreError readError = VXCORE_OK;
  const QByteArray content = getBuffer().getContentRaw(&readError);
  if (readError != VXCORE_OK) {
    showSourceError(tr("Unable to read the Typst source: %1")
                        .arg(QString::fromUtf8(vxcore_error_message(readError))));
    return;
  }

  const QString text = getBuffer().decode(content);
  if (!m_typstEditor) {
    createEditor();
  }
  applyProjectIdentity();
  m_editor->setText(text);
  m_typstEditor->setReadOnly(getBuffer().isReadOnly());
  m_editor->setModified(getBuffer().isModified());
  m_lastKnownRevision = getBuffer().getRevision();
  m_sourceAvailable = true;
  m_loadErrorBanner->hide();
  setFindAndReplaceReplaceEnabled(m_mode == ViewWindowMode::Edit && !getBuffer().isReadOnly());
  emit statusChanged();
  updatePresentationActions();
  refreshOutline(m_typstEditor->getHeadings());
}

void TypstViewWindow::showSourceError(const QString &p_error) {
  // Keep any previous draft and its history, but never expose it as a valid save snapshot.
  m_sourceAvailable = false;
  if (m_typstEditor) {
    m_typstEditor->setReadOnly(true);
  }
  m_loadErrorBanner->setText(p_error);
  m_loadErrorBanner->show();
  setFindAndReplaceReplaceEnabled(false);
  emit statusChanged();
  updatePresentationActions();
  clearOutline();
}

void TypstViewWindow::applyProjectIdentity() {
  const QString oldActiveFile = m_typstEditor->activeFile();
  const auto &project = m_controller->getProject();
  m_typstEditor->setProject(project.root, project.mainFile);
  m_typstEditor->setActiveFile(project.mainFile);
  if (oldActiveFile != project.mainFile) {
    m_typstEditor->removeFileOverlay(oldActiveFile);
  }
}

void TypstViewWindow::handleNodeRetargeted(const NodeIdentifier &p_newNodeId) {
  ViewWindow2::handleNodeRetargeted(p_newNodeId);
  const QScopedValueRollback<bool> guard(m_propagateEditorToBuffer, false);
  QString error;
  if (!m_controller->setBuffer(getBuffer(), &error)) {
    showSourceError(error);
    return;
  }
  if (m_typstEditor) {
    // Identity only: setText() here would destroy an unsaved draft and its undo history.
    applyProjectIdentity();
    m_typstEditor->setReadOnly(!m_sourceAvailable || getBuffer().isReadOnly());
  }
  clearOutline();
}

bool TypstViewWindow::aboutToClose(bool p_force) {
  const QPointer<TypstViewWindow> guard(this);
  const QPointer<TypstViewWindowController> controller(m_controller);
  const bool accepted = ViewWindow2::aboutToClose(p_force);
  if (accepted && guard && controller) {
    controller->setBuffer(Buffer2{});
  }
  return accepted;
}

QString TypstViewWindow::getLatestContent() const {
  if (!m_sourceAvailable || !m_editor) {
    throw std::runtime_error("Typst source is unavailable");
  }
  return m_editor->getText();
}

void TypstViewWindow::setModified(bool p_modified) {
  if (m_editor) {
    m_editor->setModified(p_modified);
  }
}

void TypstViewWindow::setMode(ViewWindowMode p_mode) {
  if (!m_typstEditor) {
    if (m_mode != p_mode) {
      m_mode = p_mode;
      emit modeChanged();
    }
    updatePresentationActions();
    updateFindBackend();
    return;
  }
  m_typstEditor->setMode(p_mode == ViewWindowMode::Read ? vte::VTypstEditor::Mode::Read
                                                      : vte::VTypstEditor::Mode::Edit);
}

void TypstViewWindow::syncModeFromEditor() {
  if (!m_typstEditor) {
    return;
  }
  const auto mode = m_typstEditor->mode();
  m_mode = mode == vte::VTypstEditor::Mode::Read ? ViewWindowMode::Read : ViewWindowMode::Edit;
  setFocusProxy(m_mode == ViewWindowMode::Edit ? static_cast<QWidget *>(m_editor)
                                               : static_cast<QWidget *>(m_typstEditor));
  setFindAndReplaceReplaceEnabled(m_sourceAvailable && m_mode == ViewWindowMode::Edit &&
                                  !getBuffer().isReadOnly());
  const QPointer<TypstViewWindow> guard(this);
  emit modeChanged();
  if (!guard) {
    return;
  }
  updatePresentationActions();
  updateFindBackend();
}

void TypstViewWindow::revealSource() {
  if (m_typstEditor && m_typstEditor->mode() == vte::VTypstEditor::Mode::Read) {
    setMode(ViewWindowMode::Edit);
  }
}

QSharedPointer<OutlineProvider> TypstViewWindow::getOutlineProvider() const {
  return m_outlineProvider;
}

void TypstViewWindow::setupOutlineProvider() {
  m_outlineProvider = QSharedPointer<OutlineProvider>::create();
  m_outlineProvider->setOutline(QSharedPointer<Outline>::create());
  m_outlineProvider->setAutoSectionNumberAllowed(false);
  m_outlineProvider->setReorderSupported(false);
  connect(m_outlineProvider.data(), &OutlineProvider::headingClicked, this,
          &TypstViewWindow::navigateToHeading);
}

void TypstViewWindow::clearOutline() {
  m_outlineHeadings.clear();
  // setOutline also resets the current index before notifying either outline consumer.
  const auto provider = m_outlineProvider;
  provider->setOutline(QSharedPointer<Outline>::create());
}

void TypstViewWindow::refreshOutline(const QVector<vte::TypstHeadingInfo> &p_headings) {
  if (!m_sourceAvailable || p_headings.isEmpty()) {
    clearOutline();
    return;
  }
  auto outline = QSharedPointer<Outline>::create();
  outline->m_headings.reserve(p_headings.size());
  for (const auto &heading : p_headings) {
    outline->m_headings.append(Outline::Heading(heading.m_title, heading.m_level));
  }
  m_outlineHeadings = p_headings;
  const auto provider = m_outlineProvider;
  const QPointer<TypstViewWindow> guard(this);
  provider->setOutline(outline);
  if (guard && provider->getOutline() == outline) {
    updateCurrentHeading();
  }
}

bool TypstViewWindow::isActiveFileHeading(const vte::TypstHeadingInfo &p_heading) const {
  if (!m_sourceAvailable || !m_typstEditor || p_heading.m_file.isEmpty() ||
      p_heading.m_file != m_typstEditor->activeFile()) {
    return false;
  }
  const auto *document = m_editor->document();
  const int length = document->characterCount() - 1;
  if (p_heading.m_startPos < 0 || p_heading.m_endPos < p_heading.m_startPos ||
      p_heading.m_endPos > length) {
    return false;
  }
  // QTextDocument positions are LF/UTF-16 offsets; never split a surrogate pair.
  const auto validBoundary = [document, length](int p_position) {
    return p_position == 0 || p_position == length ||
           !document->characterAt(p_position).isLowSurrogate() ||
           !document->characterAt(p_position - 1).isHighSurrogate();
  };
  return validBoundary(p_heading.m_startPos) && validBoundary(p_heading.m_endPos);
}

void TypstViewWindow::navigateToHeading(int p_index) {
  if (!m_sourceAvailable || !isEnabled() || p_index < 0 || p_index >= m_outlineHeadings.size()) {
    return;
  }
  // Both mode changes and opening another buffer can synchronously invalidate the outline.
  const auto target = m_outlineHeadings.at(p_index);
  if (target.m_file != m_typstEditor->activeFile()) {
    m_controller->requestOpenSource(target.m_file, target.m_startPos, target.m_endPos);
    return;
  }
  if (!isActiveFileHeading(target)) {
    showMessage(tr("Unable to open the Typst source location"));
    return;
  }

  const QPointer<TypstViewWindow> guard(this);
  const auto outline = m_outlineProvider->getOutline();
  const auto isCurrent = [&guard, &outline]() {
    return guard && guard->m_sourceAvailable && guard->isEnabled() &&
           guard->m_outlineProvider->getOutline() == outline;
  };
  revealSource();
  if (!isCurrent() || !isActiveFileHeading(target)) {
    return;
  }
  auto *textEdit = m_editor->getTextEdit();
  QTextCursor cursor(textEdit->document());
  cursor.setPosition(target.m_startPos);
  cursor.setPosition(target.m_endPos, QTextCursor::KeepAnchor);
  textEdit->setTextCursor(cursor);
  if (!isCurrent()) {
    return;
  }
  textEdit->ensureCursorVisible();
  if (!isCurrent()) {
    return;
  }
  textEdit->setFocus();
  if (isCurrent()) {
    m_outlineProvider->setCurrentHeadingIndex(p_index);
  }
}

void TypstViewWindow::updateCurrentHeading() {
  int index = -1;
  int start = -1;
  if (m_sourceAvailable && m_editor) {
    const int position = m_editor->getTextEdit()->textCursor().position();
    const int current = m_outlineProvider->getCurrentHeadingIndex();
    for (int i = 0; i < m_outlineHeadings.size(); ++i) {
      const auto &heading = m_outlineHeadings.at(i);
      if (isActiveFileHeading(heading) && heading.m_startPos <= position &&
          (heading.m_startPos > start || (heading.m_startPos == start && i == current))) {
        // Evaluated order need not be source order; repeated targets retain the clicked row.
        index = i;
        start = heading.m_startPos;
      }
    }
  }
  const auto provider = m_outlineProvider;
  provider->setCurrentHeadingIndex(index);
}

int TypstViewWindow::getCursorPosition() const {
  return m_editor ? m_editor->getTextEdit()->textCursor().blockNumber() : -1;
}

int TypstViewWindow::getScrollPosition() const {
  return m_editor ? m_editor->getTextEdit()->verticalScrollBar()->value() : -1;
}

ViewWindow2::ViewPositionState TypstViewWindow::capturePositionState() const {
  return m_editor ? captureEditorPositionState(m_editor->getTextEdit()) : ViewPositionState{};
}

void TypstViewWindow::restorePositionState(const ViewPositionState &p_state) {
  if (m_editor) {
    applyEditorPositionState(m_editor->getTextEdit(), p_state);
  }
}

int TypstViewWindow::getEditorMaxContentWidth() const {
  if (isViewFullScreen() || getLayoutMode() != ViewWindowLayoutMode::ReadableWidth) {
    return 0;
  }
  return getServices().get<ConfigMgr2>()->getWidgetConfig().getReadableWidthMaxPx();
}

void TypstViewWindow::applyReadableWidth() {
  if (m_editor) {
    m_editor->getTextEdit()->setMaxContentWidth(getEditorMaxContentWidth());
  }
}

void TypstViewWindow::applyEditorConfig() {
  if (!m_typstEditor) {
    return;
  }
  const QScopedValueRollback<bool> guard(m_propagateEditorToBuffer, false);
  const int scrollPosition = m_editor->getTextEdit()->verticalScrollBar()->value();
  const QTextCursor cursor = m_editor->getTextEdit()->textCursor();
  const bool modified = m_editor->isModified();
  auto config = buildEditorConfig();
  // setConfig resets the base font size, but VTextEditor retains its zoom delta.
  // Normalize that delta first, then reapply the shared source zoom to the new base font.
  m_editor->zoom(0);
  m_typstEditor->setConfig(config);
  updateEditorFromConfig();
  m_editor->setModified(modified);
  m_editor->getTextEdit()->setTextCursor(cursor);
  m_editor->getTextEdit()->verticalScrollBar()->setValue(scrollPosition);
}

void TypstViewWindow::handleEditorConfigChange() {
  ViewWindow2::handleEditorConfigChange();
  if (m_controller->checkAndUpdateConfigRevision()) {
    applyEditorConfig();
  }
}

void TypstViewWindow::handleThemeChanged() {
  ViewWindow2::handleThemeChanged();
  if (isViewFullScreen()) {
    m_presentationBackground =
        QColor(getServices().get<ThemeService>()->paletteColor(QStringLiteral("base#content#bg")));
    update();
  }
  applyEditorConfig();
}

void TypstViewWindow::updateEditorFromConfig() {
  const auto snapshot = m_controller->currentEditorConfig();
  m_editor->zoom(snapshot.zoomDelta);
  const vte::Key leaderKey(snapshot.shortcutLeaderKey);
  m_editor->setLeaderKeyToSkip(leaderKey.m_key, leaderKey.m_modifiers);
}

void TypstViewWindow::addAdditionalViewToolBarActions(QToolBar *p_toolBar) {
  const auto iconName = QStringLiteral("presentation_editor.svg");
  m_presentationAction = p_toolBar->addAction(
      ViewWindowToolBarHelper2::generateIcon(getServices(), iconName), tr("Presentation Mode"));
  m_presentationAction->setProperty("iconName", iconName);
  m_presentationAction->setCheckable(true);
  ViewWindowToolBarHelper2::addActionShortcut(
      m_presentationAction,
      getServices().get<ConfigMgr2>()->getEditorConfig().getShortcut(
          EditorConfig::Shortcut::PresentationMode),
      this);
  m_presentationEffect = new PresentationToolBarEffect(p_toolBar, this);
  connect(m_presentationAction, &QAction::triggered, this, [this]() {
    setViewFullScreen(!isViewFullScreen());
    const QSignalBlocker blocker(m_presentationAction);
    m_presentationAction->setChecked(isViewFullScreen());
  });
  connect(this, &ViewWindow2::viewFullScreenExitRequested, this,
          [this]() { setViewFullScreen(false); });
  connect(this, &ViewWindow2::viewFullScreenChanged, this, [this](bool p_on) {
    const QSignalBlocker blocker(m_presentationAction);
    m_presentationAction->setChecked(p_on);
    if (p_on) {
      m_presentationBackground = QColor(
          getServices().get<ThemeService>()->paletteColor(QStringLiteral("base#content#bg")));
    }
    m_presentationEffect->setActive(p_on);
    update();
  });
  setupPdfToolBar(p_toolBar);
}

void TypstViewWindow::paintEvent(QPaintEvent *p_event) {
  ViewWindow2::paintEvent(p_event);
  if (isViewFullScreen()) {
    QPainter painter(this);
    painter.fillRect(rect(), m_presentationBackground);
  }
}

void TypstViewWindow::scrollUp() {
  if (m_editor) {
    m_editor->getTextEdit()->verticalScrollBar()->triggerAction(
        QAbstractSlider::SliderSingleStepAdd);
  }
}

void TypstViewWindow::scrollDown() {
  if (m_editor) {
    m_editor->getTextEdit()->verticalScrollBar()->triggerAction(
        QAbstractSlider::SliderSingleStepSub);
  }
}

void TypstViewWindow::zoom(bool p_zoomIn) {
  if (m_typstEditor && m_mode == ViewWindowMode::Read) {
    m_typstEditor->setPdfZoom(m_typstEditor->pdfZoom() * (p_zoomIn ? 1.2 : 1.0 / 1.2));
    showZoomFactor(m_typstEditor->pdfZoom());
  } else if (m_editor) {
    m_editor->zoom(m_editor->zoomDelta() + (p_zoomIn ? 1 : -1));
    showZoomDelta(m_controller->persistZoomDelta(m_editor->zoomDelta()));
  }
}

void TypstViewWindow::resetZoom() {
  if (m_typstEditor && m_mode == ViewWindowMode::Read) {
    m_typstEditor->setPdfZoom(1.0);
    showZoomFactor(m_typstEditor->pdfZoom());
  } else if (m_editor) {
    m_editor->zoom(0);
    showZoomDelta(m_controller->persistZoomDelta(m_editor->zoomDelta()));
  }
}

void TypstViewWindow::handleFindTextChanged(const QString &p_text, FindOptions p_options) {
  if (m_typstEditor && m_mode == ViewWindowMode::Read) {
    if (p_text.isEmpty() || (p_options & FindOption::IncrementalSearch)) {
      m_typstEditor->setPdfSearchText(p_text);
    }
  } else if (m_sourceAvailable) {
    TextViewWindowHelper::handleFindTextChanged(this, p_text, p_options);
  }
}

void TypstViewWindow::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  if (m_typstEditor && m_mode == ViewWindowMode::Read) {
    m_typstEditor->setPdfSearchText(p_texts.value(0));
    m_typstEditor->findNextPdfMatch(p_options & FindOption::FindBackward);
  } else if (m_sourceAvailable) {
    TextViewWindowHelper::handleFindNext(this, p_texts, p_options);
  }
}

void TypstViewWindow::handleReplace(const QString &p_text, FindOptions p_options,
                                    const QString &p_replaceText) {
  if (m_sourceAvailable && m_mode == ViewWindowMode::Edit && !m_editor->isReadOnly()) {
    TextViewWindowHelper::handleReplace(this, p_text, p_options, p_replaceText);
  }
}

void TypstViewWindow::handleReplaceAll(const QString &p_text, FindOptions p_options,
                                       const QString &p_replaceText) {
  if (m_sourceAvailable && m_mode == ViewWindowMode::Edit && !m_editor->isReadOnly()) {
    TextViewWindowHelper::handleReplaceAll(this, p_text, p_options, p_replaceText);
  }
}

void TypstViewWindow::handleFindAndReplaceWidgetOpened() { updateFindBackend(); }

void TypstViewWindow::handleFindAndReplaceWidgetClosed() { clearHighlights(); }

void TypstViewWindow::clearHighlights() {
  if (m_typstEditor && m_mode == ViewWindowMode::Read) {
    m_typstEditor->setPdfSearchText(QString());
  } else if (m_editor) {
    TextViewWindowHelper::clearSearchHighlights(this);
  }
}

QPoint TypstViewWindow::getFloatingWidgetPosition() {
  return m_editor ? TextViewWindowHelper::getFloatingWidgetPosition(this)
                  : ViewWindow2::getFloatingWidgetPosition();
}

QString TypstViewWindow::selectedText() const {
  return m_sourceAvailable && m_mode == ViewWindowMode::Edit
             ? m_editor->getTextEdit()->selectedText()
             : QString();
}

void TypstViewWindow::fetchWordCountInfo(
    const std::function<void(const WordCountInfo &)> &p_callback) const {
  if (!m_sourceAvailable) {
    showMessage(tr("Typst source is unavailable"));
    return;
  }
  const QString selection = selectedText();
  auto info =
      WordCountPanel::calculateWordCount(selection.isEmpty() ? getLatestContent() : selection);
  info.m_isSelection = !selection.isEmpty();
  p_callback(info);
}

void TypstViewWindow::applySnippet(const QString &p_name) {
  if (m_sourceAvailable && !getBuffer().isReadOnly() && isEnabled()) {
    revealSource();
    TextViewWindowHelper::applySnippetByName2(this, p_name);
  }
}

void TypstViewWindow::applySnippet() {
  if (!m_sourceAvailable || getBuffer().isReadOnly() || !isEnabled()) {
    return;
  }
  revealSource();
  if (!m_editor->getTextEdit()->hasSelection() &&
      TextViewWindowHelper::detectSnippetSymbol2(this).found) {
    TextViewWindowHelper::applySnippetPrompt2(this);
    return;
  }
  const QPointer<TypstViewWindow> guard(this);
  const QString name = TextViewWindowHelper::promptForSnippet2(this);
  if (guard && !name.isEmpty()) {
    guard->applySnippet(name);
  }
}

void TypstViewWindow::applyFileOpenSettings(const FileOpenSettings &p_settings) {
  if (p_settings.m_forceMode) {
    setMode(p_settings.m_mode);
  }
  if (!m_sourceAvailable) {
    return;
  }
  if (p_settings.m_cursorOffset >= 0 || p_settings.m_searchHighlight.m_isValid) {
    revealSource();
  }
  if (p_settings.m_lineNumber >= 0) {
    m_editor->scrollToLine(p_settings.m_lineNumber, true);
  }
  if (p_settings.m_cursorOffset >= 0) {
    WidgetUtils::applyCursorOffset(m_editor->getTextEdit(), p_settings.m_cursorOffset);
  }
  if (p_settings.m_searchHighlight.m_isValid) {
    const auto result = m_editor->findText(
        p_settings.m_searchHighlight.m_patterns,
        TextViewWindowHelper::toEditorFindFlags(p_settings.m_searchHighlight.m_options), 0, -1,
        p_settings.m_searchHighlight.m_currentMatchLine);
    showFindResult(p_settings.m_searchHighlight.m_patterns, result.m_totalMatches,
                   result.m_currentMatchIndex);
  }
}
