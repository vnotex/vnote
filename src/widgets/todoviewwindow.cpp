#include "todoviewwindow.h"

#include <QApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QListView>
#include <QPointer>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextBlock>
#include <QTextCodec>
#include <QTextDocument>
#include <QTimer>

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
  setLayoutMode(ViewWindowLayoutMode::FullWidth);
  m_mode = p_mode;
  m_buffers = p_services.get<BufferService>();
  m_boundBufferId = p_buffer.id();
  m_boundGeneration = getBufferGeneration();

  m_sourceEditor = new SimpleMarkdownEditor(p_services, this);
  m_sourceEditor->setFocusPolicy(Qt::NoFocus);
  m_sourceEditor->hide();
  m_sourceEditor->setObjectName(QStringLiteral("todoSourceEditor"));
  auto *textEdit = m_sourceEditor->getTextEdit();
  textEdit->setObjectName(QStringLiteral("todoSourceTextEdit"));
  textEdit->setFocusPolicy(Qt::NoFocus);
  configureLiteralSource();

  m_controller = new TodoViewWindowController(p_services, textEdit->document(), p_buffer, this);
  m_controller->setMutationGuard([this]() { return admitMutation(); });
  m_controller->setArchiveContext(reinterpret_cast<quintptr>(this), [this](const QString &p_text) {
    QString text = p_text;
    vte::TextUtils::transformLineEnding(text, vte::LineEnding::LF, m_lineEnding);
    // Initial archive bytes retain this file's ending/codec, not notebook/git save overrides.
    const auto encoding = getBuffer().encoding();
    if (encoding.compare(QLatin1String("UTF-8"), Qt::CaseInsensitive) == 0)
      return text.toUtf8();
    auto *codec = QTextCodec::codecForName(encoding.toUtf8());
    return codec ? codec->fromUnicode(text) : text.toUtf8();
  });
  m_todoEditor = new TodoEditor(p_services, this);
  m_todoEditor->setObjectName(QStringLiteral("todoEditor"));
  m_todoEditor->bindController(m_controller);
  setCentralWidget(m_todoEditor);
  setFocusProxy(m_todoEditor);

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
  connect(m_controller, &TodoViewWindowController::documentChanged, this, [this]() {
    if (!m_loading) {
      updateDiagnostics();
      updateEditability();
    }
  });
  connect(m_controller, &TodoViewWindowController::operationRejected, this,
          [this](const QString &p_message) { showMessage(p_message); });
  connect(m_controller, &TodoViewWindowController::tasksArchived, this,
          [this](const QString &p_path) {
            // The controller reports success only after this writer's durable source save.
            m_editorDirty = false;
            setModified(false);
            emit statusChanged();
            showMessage(tr("Completed tasks archived to %1").arg(p_path));
          });
  connect(m_todoEditor, &TodoEditor::undoRequested, this, &TodoViewWindow::undo);
  connect(m_todoEditor, &TodoEditor::redoRequested, this, &TodoViewWindow::redo);
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
    m_buffers->forceAutoSave(m_boundBufferId);
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
  // Focus can return after our worker publishes but before its GUI notifications.
  // An admitted own publication must not reload the document or retire its undo history.
  if (reconcileBufferRevision()) {
    m_lastKnownRevision = m_sourceBufferRevision;
    updateEditability();
    return;
  }
  if (before.live && before.busy) {
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
  if (!m_controller || !m_todoEditor || m_updatingEditability) {
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
    if (!messages.isEmpty()) {
      messages.append(tr("Correct the task data at its source, then reload this document."));
    }
  }
  m_diagnosticBanner->setText(messages.join(QLatin1Char('\n')));
  m_diagnosticBanner->setVisible(!messages.isEmpty());
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
    if ((undoKey || redoKey) && m_todoEditor->isAncestorOf(widget) &&
        widget->window() == window()) {
      // The native editor owns local field history and forwards committed source history.
      return false;
    }
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
          showMessage(tr("Edit task fields to change this document."));
        } else {
          showFindAndReplaceWidget();
        }
      }
      return true;
    }
  }
  return ViewWindow2::eventFilter(p_object, p_event);
}

void TodoViewWindow::showFindAndReplaceWidget() { m_todoEditor->focusSearch(); }

void TodoViewWindow::handleFindNext(const QStringList &p_texts, FindOptions) {
  m_controller->setSearch(p_texts.isEmpty() ? QString() : p_texts.constFirst());
  m_todoEditor->focusSearch();
}

void TodoViewWindow::handleReplace(const QString &, FindOptions, const QString &) {
  showMessage(tr("Edit task fields to change this document."));
}

void TodoViewWindow::handleReplaceAll(const QString &p_text, FindOptions p_options,
                                      const QString &p_replaceText) {
  handleReplace(p_text, p_options, p_replaceText);
}

QString TodoViewWindow::selectedText() const { return m_todoEditor->selectedText(); }

int TodoViewWindow::getCursorPosition() const {
  const auto &document = m_controller->getDocument();
  const auto *item = document.findItem(m_controller->getSelectedItemKey());
  return item ? document.lineAt(item->m_range.m_start) - 1 : -1;
}

int TodoViewWindow::getScrollPosition() const {
  const auto *tasks = taskView(m_todoEditor);
  return tasks ? tasks->verticalScrollBar()->value() : -1;
}

ViewWindow2::ViewPositionState TodoViewWindow::capturePositionState() const {
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
  const auto &document = m_controller->getDocument();
  if (!m_sourceAvailable || !document.isValid()) {
    return;
  }
  int position = -1;
  if (p_settings.m_cursorOffset >= 0) {
    position = qMin(p_settings.m_cursorOffset, m_sourceEditor->document()->characterCount() - 1);
  } else {
    const int line = p_settings.m_searchHighlight.m_isValid &&
                             p_settings.m_searchHighlight.m_currentMatchLine >= 0
                         ? p_settings.m_searchHighlight.m_currentMatchLine
                         : p_settings.m_lineNumber;
    const auto block = m_sourceEditor->document()->findBlockByNumber(line);
    if (block.isValid()) {
      position = block.position();
    }
  }
  if (position < 0) {
    return;
  }
  for (const auto &list : document.m_lists) {
    if (position < list.m_range.m_start || position >= list.m_range.m_end) {
      continue;
    }
    QString itemKey;
    bool completed = false;
    for (const auto &item : list.m_items) {
      if (position >= item.m_range.m_start && position < item.m_range.m_end) {
        itemKey = item.m_key;
        completed = item.m_fields.m_checked;
        break;
      }
    }
    m_controller->setView(TodoViewKind::List, list.m_key);
    m_controller->setSearch(QString());
    m_controller->setTagFilter(QString());
    if (completed) {
      m_controller->setCompletedExpanded(true);
    }
    m_controller->setSelectedItemKey(itemKey);
    m_todoEditor->focusTasks(true);
    return;
  }
  for (const auto &group : document.m_groups) {
    if (position >= group.m_range.m_start && position < group.m_range.m_end) {
      const auto key = group.m_key;
      m_controller->setSelectedItemKey(QString());
      m_todoEditor->focusGroup(key);
      return;
    }
  }
}

void TodoViewWindow::scrollUp() { m_todoEditor->scroll(true); }

void TodoViewWindow::scrollDown() { m_todoEditor->scroll(false); }

// The TODO planner keeps theme/configured fonts; shared host zoom gestures are ignored.
void TodoViewWindow::zoom(bool p_zoomIn) { Q_UNUSED(p_zoomIn); }

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
}
