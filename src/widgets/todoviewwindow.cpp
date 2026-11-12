#include "todoviewwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHideEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QListView>
#include <QMenu>
#include <QPointer>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>

#include <stdexcept>

#include <controllers/todoviewwindowcontroller.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <gui/services/themeservice.h>
#include <gui/utils/widgetutils.h>
#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/textutils.h>
#include <vtextedit/theme.h>
#include <vtextedit/vtextedit.h>

#include "editors/simplemarkdowneditor.h"
#include "editors/todoeditor.h"
#include "inlinebanner.h"
#include "textviewwindowhelper.h"

using namespace vnotex;

namespace {
QString normalizedSource(QString p_source) {
  vte::TextUtils::transformLineEnding(p_source, vte::LineEnding::CRLF, vte::LineEnding::LF);
  vte::TextUtils::transformLineEnding(p_source, vte::LineEnding::CR, vte::LineEnding::LF);
  return p_source;
}

QListView *taskView(TodoEditor *p_editor) {
  return p_editor->findChild<QListView *>(QStringLiteral("todoTasks"));
}
} // namespace

TodoViewWindow::TodoViewWindow(ServiceLocator &p_services, const Buffer2 &p_buffer,
                               QWidget *p_parent, ViewWindowMode p_mode)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  setObjectName(QStringLiteral("todoViewWindow"));
  m_mode = p_mode;
  m_buffers = p_services.get<BufferService>();
  m_boundBufferId = p_buffer.id();
  m_boundGeneration = getBufferGeneration();

  m_surfaces = new QStackedWidget(this);
  m_surfaces->setObjectName(QStringLiteral("todoSurfaceStack"));
  m_sourceEditor = new SimpleMarkdownEditor(p_services, m_surfaces);
  m_sourceEditor->setObjectName(QStringLiteral("todoSourceEditor"));
  m_sourceEditor->setAccessibleName(tr("TODO Markdown source"));
  auto *textEdit = m_sourceEditor->getTextEdit();
  textEdit->setObjectName(QStringLiteral("todoSourceTextEdit"));
  textEdit->setAccessibleName(tr("TODO Markdown source"));
  configureLiteralSource();

  m_controller = new TodoViewWindowController(p_services, textEdit->document(), p_buffer, this);
  m_controller->setMutationGuard([this]() { return admitMutation(); });
  m_todoEditor = new TodoEditor(p_services, m_surfaces);
  m_todoEditor->setObjectName(QStringLiteral("todoEditor"));
  m_todoEditor->bindController(m_controller);
  m_surfaces->addWidget(m_todoEditor);
  m_surfaces->addWidget(m_sourceEditor);
  setCentralWidget(m_surfaces);
  setFocusProxy(m_todoEditor);

  setupToolBar();
  m_diagnosticBanner = new InlineBanner(this);
  m_diagnosticBanner->setObjectName(QStringLiteral("todoDiagnosticBanner"));
  m_diagnosticBanner->setAccessibleName(tr("TODO source diagnostic"));
  m_diagnosticBanner->setSeverity(InlineBanner::Severity::Error);
  addTopWidget(m_diagnosticBanner);
  m_diagnosticBanner->hide();

  connect(textEdit, &vte::VTextEdit::contentsChanged, this, [this]() {
    if (!m_loading && m_sourceAvailable) {
      onEditorContentsChanged();
    }
  });
  connect(textEdit->document(), &QTextDocument::undoAvailable, this,
          [this]() { updateEditability(); });
  connect(textEdit->document(), &QTextDocument::redoAvailable, this,
          [this]() { updateEditability(); });
  connect(m_controller, &TodoViewWindowController::documentChanged, this, [this]() {
    if (!m_loading) {
      updateDiagnostics();
      updateEditability();
    }
  });
  connect(m_controller, &TodoViewWindowController::operationRejected, this,
          [this](const QString &p_message) { showMessage(p_message); });
  connect(m_todoEditor, &TodoEditor::undoRequested, this, &TodoViewWindow::undo);
  connect(m_todoEditor, &TodoEditor::redoRequested, this, &TodoViewWindow::redo);
  connect(m_todoEditor, &TodoEditor::requestSourceMode, this, [this]() { setSourceMode(true); });
  connect(this, &ViewWindow2::statusChanged, this, [this]() { updateEditability(); });
  connect(this, &ViewWindow2::focused, this, [this](ViewWindow2 *) {
    updateToday();
    updateEditability();
  });

  // SimpleMarkdownEditor refreshes its own config first. Restore literal-source
  // policy afterwards, only for this instance (projected note fields keep theirs).
  connect(p_services.get<ThemeService>(), &ThemeService::themeChanged, this,
          [this]() { configureLiteralSource(); });

  m_dayTimer = new QTimer(this);
  m_dayTimer->setInterval(60000);
  connect(m_dayTimer, &QTimer::timeout, this, [this]() {
    if (isVisible() && m_boundGeneration == getBufferGeneration()) {
      updateToday();
    }
  });

  // The controller initially sees the empty editor, not the incoming file.
  // Establish its first real projection only after loading that file below.
  if (m_buffers) {
    connect(m_buffers->asQObject(), SIGNAL(bufferPublicationChanged(QString)), this,
            SLOT(handleBufferPublicationChanged(QString)));
    registerSnapshotWriter();
  }
  m_controller->setBuffer(p_buffer);
  syncEditorFromBuffer();
  setupShortcuts();
  qApp->installEventFilter(this);
}

TodoViewWindow::~TodoViewWindow() {
  qApp->removeEventFilter(this);
  m_loading = true;
  m_controller->setMutationGuard({});
  if (m_buffers) {
    disconnect(m_buffers->asQObject(), nullptr, this, nullptr);
    m_buffers->unregisterActiveWriter(getBuffer().id(), reinterpret_cast<quintptr>(this));
    m_buffers->revokeSnapshotWriter(m_snapshotWriterToken);
  }
}

void TodoViewWindow::setupToolBar() {
  auto *bar = createToolBar(this);
  bar->setObjectName(QStringLiteral("todoToolBar"));
  addToolBar(bar);
  addLeftCommonToolBarActions(bar);

  auto *surfaces = new QActionGroup(this);
  surfaces->setExclusive(true);
  m_tasksAction = bar->addAction(tr("Tasks"));
  m_tasksAction->setObjectName(QStringLiteral("todoTasksAction"));
  m_sourceAction = bar->addAction(tr("Source"));
  m_sourceAction->setObjectName(QStringLiteral("todoSourceAction"));
  for (auto *action : {m_tasksAction, m_sourceAction}) {
    action->setCheckable(true);
    surfaces->addAction(action);
  }
  m_tasksAction->setChecked(true);
  connect(m_tasksAction, &QAction::triggered, this, [this]() { setSourceMode(false); });
  connect(m_sourceAction, &QAction::triggered, this, [this]() { setSourceMode(true); });

  const auto actions = TextViewWindowHelper::addUndoRedoActions(bar, getServices(), this);
  m_undoAction = actions.first;
  m_redoAction = actions.second;
  m_undoAction->setObjectName(QStringLiteral("todoUndoAction"));
  m_redoAction->setObjectName(QStringLiteral("todoRedoAction"));
  connect(m_undoAction, &QAction::triggered, this, &TodoViewWindow::undo);
  connect(m_redoAction, &QAction::triggered, this, &TodoViewWindow::redo);
  addRightCommonToolBarActions(bar);
}

void TodoViewWindow::configureLiteralSource() {
  if (!m_sourceEditor) {
    return;
  }
  const QScopedValueRollback<bool> loading(m_loading, true);
  auto *theme = getServices().get<ThemeService>();
  auto text = QSharedPointer<vte::TextEditorConfig>::create();
  text->m_theme = vte::Theme::createThemeFromContent(theme->fetchTextEditorStyle());
  text->m_syntaxTheme = theme->getEditorHighlightTheme();
  text->m_scaleFactor = WidgetUtils::calculateScaleFactor();
  text->m_lineNumberType = vte::VTextEditor::LineNumberType::None;
  text->m_textFoldingEnabled = false;
  text->m_lineEndingPolicy = vte::LineEndingPolicy::LF;
  auto markdown = QSharedPointer<vte::MarkdownEditorConfig>::create(text);
  markdown->m_inplacePreviewSources = vte::MarkdownEditorConfig::NoInplacePreview;
  markdown->m_concealRules.clear();
  m_sourceEditor->setConfig(markdown);
  m_sourceEditor->setInplacePreviewEnabled(false);
}

void TodoViewWindow::registerSnapshotWriter() {
  if (!m_buffers)
    return;
  m_buffers->revokeSnapshotWriter(m_snapshotWriterToken);
  m_snapshotWriterToken = 0;
  if (!m_boundBufferId.isEmpty()) {
    m_snapshotWriterToken = m_buffers->registerSnapshotWriter(
        m_boundBufferId, this, reinterpret_cast<quintptr>(this),
        [this]() { return m_sourceAvailable ? m_sourceBufferRevision : -1; });
  }
}

void TodoViewWindow::requestSourceRefresh() const {
  m_refreshPending = true;
  if (m_refreshQueued || m_loading)
    return;
  m_refreshQueued = true;
  const QPointer<TodoViewWindow> window(const_cast<TodoViewWindow *>(this));
  const auto generation = getBufferGeneration();
  const auto bufferId = m_boundBufferId;
  QMetaObject::invokeMethod(
      window.data(),
      [window, generation, bufferId]() {
        if (!window || window->getBufferGeneration() != generation ||
            window->m_boundBufferId != bufferId)
          return;
        window->m_refreshQueued = false;
        if (!window->m_refreshPending || window->m_loading || !window->m_buffers)
          return;
        const auto publication =
            window->m_buffers->snapshotPublication(bufferId, window->m_snapshotWriterToken);
        if (!publication.live) {
          window->m_sourceAvailable = false;
          window->m_waitingForSource = false;
          window->m_refreshPending = false;
        } else if (window->reconcileBufferRevision()) {
          // A foreign pending attempt may have failed without changing source. An
          // own publication must not replace a newer local draft with older bytes.
          window->m_refreshPending = false;
          window->m_waitingForSource = false;
        } else if (publication.busy) {
          window->m_waitingForSource = true;
          // Completion/queue-state notification wakes us; never poll raw core data.
        } else {
          window->syncEditorFromBufferPreservingPosition();
          return;
        }
        window->updateDiagnostics();
        window->updateEditability();
      },
      Qt::QueuedConnection);
}

void TodoViewWindow::handleBufferPublicationChanged(const QString &p_bufferId) {
  if (p_bufferId != m_boundBufferId || m_loading || !m_buffers)
    return;
  if (m_refreshPending || !m_sourceAvailable)
    requestSourceRefresh();
  updateEditability();
}

void TodoViewWindow::syncEditorFromBuffer() {
  if (!m_buffers)
    return;
  const auto before = m_buffers->snapshotPublication(m_boundBufferId, m_snapshotWriterToken);
  if (before.live && before.busy) {
    if (reconcileBufferRevision()) {
      m_lastKnownRevision = m_sourceBufferRevision;
      updateEditability();
      return;
    }
    m_waitingForSource = true;
    m_refreshPending = true;
    m_loading = false;
    m_controller->setSourceRefreshEnabled(false);
    m_controller->setEditable(false);
    m_todoEditor->setEditable(false);
    requestSourceRefresh();
    updateDiagnostics();
    updateEditability();
    return;
  }

  m_loading = true;
  m_todoEditor->invalidateGestures();
  m_controller->endCoalescing();
  m_controller->setSourceRefreshEnabled(false);
  m_controller->setEditable(false);
  m_todoEditor->setEditable(false);
  m_waitingForSource = false;
  m_refreshPending = false;

  VxCoreError error = VXCORE_ERR_INVALID_STATE;
  const auto raw = before.live ? getBuffer().getContentRaw(&error) : QByteArray();
  const auto after = m_buffers->snapshotPublication(m_boundBufferId, m_snapshotWriterToken);
  if (after.live && (after.busy || after.bufferEpoch != before.bufferEpoch)) {
    m_waitingForSource = true;
    m_loading = false;
    requestSourceRefresh();
    updateDiagnostics();
    updateEditability();
    return;
  }
  m_sourceAvailable = getBuffer().isValid() && after.live && error == VXCORE_OK;
  m_originalSource = m_sourceAvailable ? getBuffer().decode(raw) : QString();
  m_originalNormalizedSource = normalizedSource(m_originalSource);
  m_lineEnding = m_originalSource.isEmpty() ? vte::LineEnding::LF
                                            : vte::TextUtils::detectLineEnding(m_originalSource);
  m_sourceEditor->setPlainText(m_originalNormalizedSource);
  m_sourceEditor->getTextEdit()->document()->clearUndoRedoStacks();
  m_sourceEditor->setModified(m_sourceAvailable && getBuffer().isModified());
  if (m_sourceAvailable) {
    m_bufferEpoch = after.bufferEpoch;
    m_sourceBufferRevision = after.coreRevision;
    m_sourceWriterToken = after.writerToken;
  }
  m_lastKnownRevision = m_sourceBufferRevision;
  m_editorDirty = false;
  m_controller->setSourceRefreshEnabled(true);
  m_loading = false;
  updateToday();
  updateDiagnostics();
  updateEditability();
}

bool TodoViewWindow::reconcileBufferRevision() const {
  const auto &buffer = getBuffer();
  if (!m_buffers || !m_sourceAvailable || !buffer.isValid() || buffer.id() != m_boundBufferId ||
      getBufferGeneration() != m_boundGeneration)
    return false;
  const auto publication = m_buffers->snapshotPublication(m_boundBufferId, m_snapshotWriterToken);
  if (!publication.live || publication.bufferEpoch != m_bufferEpoch || publication.foreignPending ||
      (publication.busy && !publication.ownPending))
    return false;
  const bool ownPublication =
      m_snapshotWriterToken && publication.writerToken == m_snapshotWriterToken;
  if (publication.coreRevision == m_sourceBufferRevision) {
    // Memory publication can change ownership without changing bytes or revision.
    if (!ownPublication && publication.writerToken != m_sourceWriterToken)
      return false;
  } else {
    if (!ownPublication || publication.chainStartRevision > m_sourceBufferRevision ||
        publication.coreRevision < m_sourceBufferRevision)
      return false;
    m_sourceBufferRevision = publication.coreRevision;
  }
  // An own zero-delta publication also retires the previously accepted foreign token.
  m_sourceWriterToken = publication.writerToken;
  return true;
}

QString TodoViewWindow::getLatestContent() const {
  if (!m_sourceAvailable || !m_buffers ||
      !m_buffers->snapshotPublication(m_boundBufferId, m_snapshotWriterToken).live) {
    requestSourceRefresh();
    throw std::runtime_error("The note source is unavailable");
  }
  if (!reconcileBufferRevision()) {
    requestSourceRefresh();
    throw std::runtime_error(
        "The TODO source is changing in another view; please retry after refresh");
  }
  auto source = m_sourceEditor->toPlainText();
  // Modified flags are cleared by save/autosave; only actual text equality
  // proves that returning the original line-ending spelling is still correct.
  if (source == m_originalNormalizedSource)
    return m_originalSource;
  vte::TextUtils::transformLineEnding(source, vte::LineEnding::LF, m_lineEnding);
  return source;
}

bool TodoViewWindow::canEditSource() const {
  const auto &buffer = getBuffer();
  if (m_loading || m_bindingBuffer || !m_sourceAvailable || !isEnabled() ||
      !m_sourceEditor->isEnabled() || getMode() != ViewWindowMode::Edit || !buffer.isValid() ||
      buffer.isReadOnly() || buffer.id() != m_boundBufferId ||
      getBufferGeneration() != m_boundGeneration || !m_buffers)
    return false;
  const auto publication = m_buffers->snapshotPublication(m_boundBufferId, m_snapshotWriterToken);
  return publication.live && !publication.foreignPending &&
         (!publication.busy || publication.ownPending);
}

void TodoViewWindow::updateEditability() {
  if (!m_controller || !m_todoEditor || !m_undoAction || m_updatingEditability) {
    return;
  }
  const QScopedValueRollback<bool> updating(m_updatingEditability, true);
  if (!m_loading) {
    // Base autosave notifications reach every view, not only the active writer.
    // Restore the last loaded/verified-own base, never an observed foreign stamp.
    const bool current = reconcileBufferRevision();
    m_lastKnownRevision = m_sourceBufferRevision;
    if (m_buffers) {
      const auto publication =
          m_buffers->snapshotPublication(m_boundBufferId, m_snapshotWriterToken);
      if (publication.live && publication.foreignPending && !m_waitingForSource) {
        m_waitingForSource = true;
        requestSourceRefresh();
        updateDiagnostics();
      } else if (current && m_waitingForSource) {
        m_waitingForSource = false;
        updateDiagnostics();
      } else if (m_sourceAvailable && publication.live && !publication.busy && !current &&
                 !m_refreshPending && ownsWidget(QApplication::focusWidget())) {
        requestSourceRefresh();
      }
    }
  }
  const bool editable = canEditSource();
  m_sourceEditor->setReadOnly(!editable);
  const bool tasksEditable = editable && m_controller->getDocument().isValid();
  m_controller->setEditable(tasksEditable);
  m_todoEditor->setEditable(tasksEditable);
  auto *document = m_sourceEditor->getTextEdit()->document();
  m_undoAction->setEnabled(editable && document->isUndoAvailable());
  m_redoAction->setEnabled(editable && document->isRedoAvailable());
  setFindAndReplaceReplaceEnabled(editable);
}

bool TodoViewWindow::admitMutation() {
  if (!canEditSource()) {
    updateEditability();
    return false;
  }
  if (!reconcileBufferRevision()) {
    // Refuse the captured gesture; the base reload/position path refreshes the
    // source, but this call never replays an old row/index on the new snapshot.
    m_controller->endCoalescing();
    syncEditorFromBufferPreservingPosition();
    showMessage(tr("The document changed in another view. Please try again."));
    return false;
  }
  m_lastKnownRevision = m_sourceBufferRevision;
  return true;
}

void TodoViewWindow::setModified(bool p_modified) {
  if (!m_sourceEditor) {
    return;
  }
  m_sourceEditor->setModified(p_modified);
  if (!m_loading) {
    updateEditability();
  }
}

void TodoViewWindow::setMode(ViewWindowMode p_mode) {
  const bool changed = m_mode != p_mode;
  m_mode = p_mode;
  m_controller->endCoalescing();
  updateEditability();
  if (changed) {
    emit modeChanged();
  }
}

void TodoViewWindow::applySurface(bool p_source, bool p_focus) {
  m_sourceMode = p_source;
  m_surfaces->setCurrentWidget(p_source ? static_cast<QWidget *>(m_sourceEditor)
                                        : static_cast<QWidget *>(m_todoEditor));
  setFocusProxy(p_source ? static_cast<QWidget *>(m_sourceEditor)
                         : static_cast<QWidget *>(m_todoEditor));
  const QSignalBlocker tasksBlocker(m_tasksAction);
  const QSignalBlocker sourceBlocker(m_sourceAction);
  m_tasksAction->setChecked(!p_source);
  m_sourceAction->setChecked(p_source);
  if (p_focus && isVisible()) {
    if (p_source) {
      m_sourceEditor->getTextEdit()->setFocus(Qt::OtherFocusReason);
    } else {
      m_todoEditor->focusCurrent();
    }
  }
}

void TodoViewWindow::setSourceMode(bool p_source) {
  m_controller->endCoalescing();
  if (!p_source) {
    m_controller->refreshFromSource();
    if (!m_sourceAvailable || !m_controller->getDocument().isValid()) {
      updateDiagnostics();
      applySurface(true, true);
      return;
    }
    hideFindAndReplaceWidget();
  }
  applySurface(p_source, true);
  updateEditability();
}

void TodoViewWindow::updateDiagnostics() {
  QStringList messages;
  m_diagnosticBanner->setSeverity(m_waitingForSource ? InlineBanner::Severity::Info
                                                     : InlineBanner::Severity::Error);
  if (m_waitingForSource) {
    messages.append(tr("Waiting for another view to finish saving this document"));
  } else if (!m_sourceAvailable) {
    messages.append(tr("The note source is unavailable. Reload the document to try again."));
  } else {
    for (const auto &diagnostic : m_controller->getDocument().m_diagnostics) {
      messages.append(tr("Line %1: %2").arg(diagnostic.m_line).arg(diagnostic.m_message));
    }
  }
  m_diagnosticBanner->setText(messages.join(QLatin1Char('\n')));
  m_diagnosticBanner->setVisible(!messages.isEmpty());
  if (!messages.isEmpty() && !m_waitingForSource) {
    const bool hadTaskFocus = ownsWidget(QApplication::focusWidget()) && !m_sourceMode;
    applySurface(true, hadTaskFocus);
  }
}

void TodoViewWindow::undo() {
  if (!admitMutation()) {
    return;
  }
  m_controller->endCoalescing();
  m_sourceEditor->getTextEdit()->undo();
  m_controller->refreshFromSource();
  updateDiagnostics();
  updateEditability();
}

void TodoViewWindow::redo() {
  if (!admitMutation()) {
    return;
  }
  m_controller->endCoalescing();
  m_sourceEditor->getTextEdit()->redo();
  m_controller->refreshFromSource();
  updateDiagnostics();
  updateEditability();
}

void TodoViewWindow::updateToday() {
  if (m_controller) {
    m_controller->setToday(QDate::currentDate());
  }
}

void TodoViewWindow::showEvent(QShowEvent *p_event) {
  ViewWindow2::showEvent(p_event);
  updateToday();
  m_dayTimer->start();
}

void TodoViewWindow::hideEvent(QHideEvent *p_event) {
  m_dayTimer->stop();
  m_controller->endCoalescing();
  ViewWindow2::hideEvent(p_event);
}

void TodoViewWindow::changeEvent(QEvent *p_event) {
  ViewWindow2::changeEvent(p_event);
  if (p_event->type() == QEvent::EnabledChange) {
    updateEditability();
  } else if (p_event->type() == QEvent::ActivationChange && isActiveWindow()) {
    updateToday();
  }
}

bool TodoViewWindow::ownsWidget(const QWidget *p_widget) const {
  for (auto *widget = p_widget; widget; widget = widget->parentWidget()) {
    if (auto *view = qobject_cast<const ViewWindow2 *>(widget)) {
      return view == this;
    }
  }
  return false;
}

bool TodoViewWindow::eventFilter(QObject *p_object, QEvent *p_event) {
  auto *widget = qobject_cast<QWidget *>(p_object);
  if (!m_controller || !ownsWidget(widget)) {
    return ViewWindow2::eventFilter(p_object, p_event);
  }
  if (p_event->type() == QEvent::FocusOut) {
    m_controller->endCoalescing();
  }
  if (p_event->type() == QEvent::ShortcutOverride || p_event->type() == QEvent::KeyPress) {
    auto *key = static_cast<QKeyEvent *>(p_event);
    const bool undoKey = key->matches(QKeySequence::Undo);
    const bool redoKey = key->matches(QKeySequence::Redo);
    const bool findKey = key->matches(QKeySequence::Find);
    const bool replaceKey = key->matches(QKeySequence::Replace);
    if (undoKey || redoKey || findKey || replaceKey) {
      key->accept();
      if (p_event->type() == QEvent::KeyPress && isEnabled()) {
        if (undoKey) {
          undo();
        } else if (redoKey) {
          redo();
        } else if (replaceKey) {
          setSourceMode(true);
          showFindAndReplaceWidget();
        } else {
          showFindAndReplaceWidget();
        }
      }
      return true;
    }
  }

  auto *text = m_sourceEditor->getTextEdit();
  const bool sourceInput = widget == text || text->isAncestorOf(widget);
  if (sourceInput && p_event->type() == QEvent::ContextMenu) {
    auto *context = static_cast<QContextMenuEvent *>(p_event);
    showSourceContextMenu(context->globalPos());
    context->accept();
    return true;
  }
  if (sourceInput && p_event->type() == QEvent::KeyPress) {
    const auto *key = static_cast<QKeyEvent *>(p_event);
    const bool changesText =
        key->matches(QKeySequence::Cut) || key->matches(QKeySequence::Paste) ||
        key->key() == Qt::Key_Backspace || key->key() == Qt::Key_Delete ||
        key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
        (key->key() == Qt::Key_Tab && !(key->modifiers() & Qt::ControlModifier)) ||
        (!key->text().isEmpty() && key->text().front().isPrint() &&
         !key->matches(QKeySequence::Copy) && !key->matches(QKeySequence::SelectAll));
    if (changesText && !text->isReadOnly() && !admitMutation()) {
      p_event->accept();
      return true;
    }
  }
  if (sourceInput && !text->isReadOnly() &&
      (p_event->type() == QEvent::Drop ||
       (p_event->type() == QEvent::InputMethod &&
        (!static_cast<QInputMethodEvent *>(p_event)->commitString().isEmpty() ||
         static_cast<QInputMethodEvent *>(p_event)->replacementLength() != 0))) &&
      !admitMutation()) {
    p_event->accept();
    return true;
  }
  return ViewWindow2::eventFilter(p_object, p_event);
}

void TodoViewWindow::showSourceContextMenu(const QPoint &p_globalPosition) {
  QMenu menu(this);
  auto *text = m_sourceEditor->getTextEdit();
  const bool editable = canEditSource();
  const bool selection = text->textCursor().hasSelection();
  auto *undoAction = menu.addAction(tr("Undo"));
  undoAction->setEnabled(editable && text->document()->isUndoAvailable());
  auto *redoAction = menu.addAction(tr("Redo"));
  redoAction->setEnabled(editable && text->document()->isRedoAvailable());
  menu.addSeparator();
  auto *cutAction = menu.addAction(tr("Cut"));
  cutAction->setEnabled(editable && selection);
  auto *copyAction = menu.addAction(tr("Copy"));
  copyAction->setEnabled(selection);
  auto *pasteAction = menu.addAction(tr("Paste"));
  pasteAction->setEnabled(editable && text->canPaste());
  auto *deleteAction = menu.addAction(tr("Delete"));
  deleteAction->setEnabled(editable && selection);
  menu.addSeparator();
  auto *selectAllAction = menu.addAction(tr("Select All"));
  const QPointer<TodoViewWindow> guard(this);
  const auto generation = getBufferGeneration();
  const auto revision = text->document()->revision();
  auto *chosen = menu.exec(p_globalPosition);
  if (!guard || generation != getBufferGeneration() || !chosen) {
    return;
  }
  if (chosen == copyAction) {
    text->copy();
  } else if (chosen == selectAllAction) {
    text->selectAll();
  } else if (revision == text->document()->revision() && admitMutation()) {
    m_controller->endCoalescing();
    if (chosen == undoAction) {
      undo();
    } else if (chosen == redoAction) {
      redo();
    } else if (chosen == cutAction) {
      text->cut();
    } else if (chosen == pasteAction) {
      text->paste();
    } else if (chosen == deleteAction) {
      auto cursor = text->textCursor();
      cursor.removeSelectedText();
    }
  }
}

void TodoViewWindow::showFindAndReplaceWidget() {
  if (!m_sourceMode) {
    m_todoEditor->focusSearch();
    return;
  }
  ViewWindow2::showFindAndReplaceWidget();
  updateEditability();
}

void TodoViewWindow::handleFindTextChanged(const QString &p_text, FindOptions p_options) {
  if (p_options & FindOption::IncrementalSearch) {
    m_sourceEditor->peekText(p_text, TextViewWindowHelper::toEditorFindFlags(p_options));
  }
}

void TodoViewWindow::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  if (!m_sourceMode) {
    setSourceMode(true);
  }
  const auto result =
      m_sourceEditor->findText(p_texts, TextViewWindowHelper::toEditorFindFlags(p_options));
  showFindResult(p_texts, result.m_totalMatches, result.m_currentMatchIndex);
}

void TodoViewWindow::handleReplace(const QString &p_text, FindOptions p_options,
                                   const QString &p_replaceText) {
  if (!m_sourceMode) {
    setSourceMode(true);
  }
  if (!admitMutation()) {
    return;
  }
  m_controller->endCoalescing();
  const auto result = m_sourceEditor->replaceText(
      p_text, TextViewWindowHelper::toEditorFindFlags(p_options), p_replaceText);
  showReplaceResult(p_text, result.m_totalMatches);
}

void TodoViewWindow::handleReplaceAll(const QString &p_text, FindOptions p_options,
                                      const QString &p_replaceText) {
  if (!m_sourceMode) {
    setSourceMode(true);
  }
  if (!admitMutation()) {
    return;
  }
  m_controller->endCoalescing();
  const auto result = m_sourceEditor->replaceAll(
      p_text, TextViewWindowHelper::toEditorFindFlags(p_options), p_replaceText);
  showReplaceResult(p_text, result.m_totalMatches);
}

void TodoViewWindow::clearHighlights() {
  m_sourceEditor->clearIncrementalSearchHighlight();
  m_sourceEditor->clearSearchHighlight();
}

void TodoViewWindow::handleFindAndReplaceWidgetClosed() { clearHighlights(); }

void TodoViewWindow::handleFindAndReplaceWidgetOpened() { updateEditability(); }

QString TodoViewWindow::selectedText() const {
  return m_sourceMode ? m_sourceEditor->getTextEdit()->selectedText()
                      : m_todoEditor->selectedText();
}

int TodoViewWindow::getCursorPosition() const {
  if (m_sourceMode) {
    return m_sourceEditor->getTextEdit()->textCursor().blockNumber();
  }
  const auto &document = m_controller->getDocument();
  const auto *item = document.findItem(m_controller->getSelectedItemKey());
  return item ? document.lineAt(item->m_range.m_start) - 1 : -1;
}

int TodoViewWindow::getScrollPosition() const {
  if (m_sourceMode) {
    return m_sourceEditor->getTextEdit()->verticalScrollBar()->value();
  }
  const auto *tasks = taskView(m_todoEditor);
  return tasks ? tasks->verticalScrollBar()->value() : -1;
}

ViewWindow2::ViewPositionState TodoViewWindow::capturePositionState() const {
  if (m_sourceMode) {
    return captureEditorPositionState(m_sourceEditor->getTextEdit());
  }
  ViewPositionState state;
  if (const auto *tasks = taskView(m_todoEditor)) {
    state.m_scrollValue = tasks->verticalScrollBar()->value();
    state.m_scrollMax = tasks->verticalScrollBar()->maximum();
  }
  state.m_cursorLine = getCursorPosition();
  state.m_cursorPositionInBlock = 0;
  return state;
}

void TodoViewWindow::restorePositionState(const ViewPositionState &p_state) {
  if (m_sourceMode) {
    applyEditorPositionState(m_sourceEditor->getTextEdit(), p_state);
    return;
  }
  if (auto *tasks = taskView(m_todoEditor); tasks && p_state.m_scrollValue >= 0) {
    auto *bar = tasks->verticalScrollBar();
    const int value =
        p_state.m_scrollMax > 0
            ? qRound(qreal(p_state.m_scrollValue) * bar->maximum() / p_state.m_scrollMax)
            : p_state.m_scrollValue;
    bar->setValue(value);
  }
}

void TodoViewWindow::applyFileOpenSettings(const FileOpenSettings &p_settings) {
  // Replacement needs the TODO default; navigation on an existing tab must
  // retain its explicit Read/Edit choice unless the request forces a mode.
  if (p_settings.m_forceMode || m_bindingBuffer) {
    m_bindingBuffer = false;
    setMode(p_settings.m_forceMode ? p_settings.m_mode : ViewWindowMode::Edit);
  }
  if (p_settings.m_lineNumber >= 0 || p_settings.m_cursorOffset >= 0 ||
      p_settings.m_searchHighlight.m_isValid) {
    setSourceMode(true);
  }
  if (p_settings.m_lineNumber >= 0) {
    m_sourceEditor->scrollToLine(p_settings.m_lineNumber, true);
  }
  if (p_settings.m_cursorOffset >= 0 && !m_sourceEditor->isReadOnly()) {
    WidgetUtils::applyCursorOffset(m_sourceEditor->getTextEdit(), p_settings.m_cursorOffset);
  }
  if (p_settings.m_searchHighlight.m_isValid) {
    const auto result = m_sourceEditor->findText(
        p_settings.m_searchHighlight.m_patterns,
        TextViewWindowHelper::toEditorFindFlags(p_settings.m_searchHighlight.m_options), 0, -1,
        p_settings.m_searchHighlight.m_currentMatchLine);
    showFindResult(p_settings.m_searchHighlight.m_patterns, result.m_totalMatches,
                   result.m_currentMatchIndex);
  }
}

void TodoViewWindow::scrollUp() {
  if (m_sourceMode) {
    m_sourceEditor->getTextEdit()->verticalScrollBar()->triggerAction(
        QAbstractSlider::SliderSingleStepSub);
  } else {
    m_todoEditor->scroll(true);
  }
}

void TodoViewWindow::scrollDown() {
  if (m_sourceMode) {
    m_sourceEditor->getTextEdit()->verticalScrollBar()->triggerAction(
        QAbstractSlider::SliderSingleStepAdd);
  } else {
    m_todoEditor->scroll(false);
  }
}

void TodoViewWindow::zoom(bool p_zoomIn) {
  if (m_sourceMode) {
    m_sourceEditor->zoom(m_sourceEditor->zoomDelta() + (p_zoomIn ? 1 : -1));
    showZoomDelta(m_sourceEditor->zoomDelta());
  } else {
    m_todoEditor->zoom(p_zoomIn ? 1 : -1);
    showZoomDelta(m_todoEditor->getZoomDelta());
  }
}

void TodoViewWindow::resetZoom() {
  if (m_sourceMode) {
    m_sourceEditor->zoom(0);
  } else {
    m_todoEditor->zoom(-m_todoEditor->getZoomDelta());
  }
  showZoomDelta(0);
}

void TodoViewWindow::prepareBufferReplacement() {
  m_controller->endCoalescing();
  m_todoEditor->resetForBuffer();
}

void TodoViewWindow::releaseBufferResources() {
  m_loading = true;
  if (m_buffers)
    m_buffers->revokeSnapshotWriter(m_snapshotWriterToken);
  m_snapshotWriterToken = 0;
  m_refreshQueued = false;
  m_refreshPending = false;
  m_controller->setSourceRefreshEnabled(false);
  m_controller->setEditable(false);
  m_controller->setBuffer(Buffer2());
  m_todoEditor->resetForBuffer();
  m_sourceAvailable = false;
  m_boundBufferId.clear();
  m_bufferEpoch = 0;
  m_sourceBufferRevision = 0;
  m_sourceWriterToken = 0;
  m_waitingForSource = false;
}

void TodoViewWindow::bindBufferResources() {
  m_bindingBuffer = true;
  m_boundBufferId = getBuffer().id();
  m_boundGeneration = getBufferGeneration();
  m_controller->setBuffer(getBuffer());
  registerSnapshotWriter();
  applySurface(false, false);
}
