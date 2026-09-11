#include "bufferservice.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QPointer>
#include <QTextCodec>
#include <QThread>
#include <QTimer>
#include <QtGlobal>
#include <atomic>

#include <core/fileopensettings.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/buffersavequeue.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookiogate.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {
Q_LOGGING_CATEGORY(perfSave, "vnote.perf.save")

// Resolve a codec by name, falling back to UTF-8 when the name is empty or
// unknown. Never returns null (QTextCodec::codecForName("UTF-8") always exists).
QTextCodec *resolveCodec(const QString &p_codecName) {
  if (!p_codecName.isEmpty()) {
    if (QTextCodec *codec = QTextCodec::codecForName(p_codecName.toUtf8())) {
      return codec;
    }
  }
  return QTextCodec::codecForName("UTF-8");
}
} // namespace

namespace vnotex {
struct ProtectedBufferState {
  BufferService *owner = nullptr;
  QString bufferId;
  NodeIdentifier nodeId;
  QString editorType;
  quint64 generation = 0;
  quint64 lastQueuedRevision = 0;
  std::atomic<bool> current{true};
  QMutex mutex;
  size_t operations = 0;
  std::atomic<bool> readOnly{false};
  bool locking = false;
};
} // namespace vnotex

struct BufferService::ProtectedBuffers {
  QHash<QString, std::shared_ptr<ProtectedBufferState>> buffers;
};

struct BufferService::NoteConversions {
  struct Entry {
    ActiveWriter writer;
    bool hadWriter = false;
    bool wasDirty = false;
  };
  QHash<QString, Entry> entries;
};

ProtectedBufferLease::ProtectedBufferLease(std::shared_ptr<ProtectedBufferState> p_state)
    : m_state(std::move(p_state)) {}

ProtectedBufferLease::~ProtectedBufferLease() {
  if (!m_acquired) {
    return;
  }
  QMutexLocker lock(&m_state->mutex);
  Q_ASSERT(m_state->operations > 0);
  --m_state->operations;
}

bool ProtectedBufferLease::isCurrent() const noexcept {
  return m_acquired && m_state->current.load(std::memory_order_acquire);
}

QString ProtectedBufferLease::bufferId() const { return m_state->bufferId; }
quint64 ProtectedBufferLease::generation() const { return m_state->generation; }

QString Buffer2::editorType() const {
  return m_protectedState && m_protectedState->current.load(std::memory_order_acquire)
             ? m_protectedState->editorType
             : QString();
}

void BufferService::updateProtectedNodeId(const Buffer2 &p_buffer, const NodeIdentifier &p_nodeId) {
  const auto &state = p_buffer.m_protectedState;
  QMutexLocker lock(&state->mutex);
  if (state->nodeId.notebookId != p_nodeId.notebookId) {
    // A cross-notebook move must close/reopen after its key rewrap; old jobs
    // must not run under their previous notebook's IO gate.
    state->current.store(false, std::memory_order_release);
  } else {
    state->nodeId.relativePath = p_nodeId.relativePath;
  }
}

BufferService::BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                             NotebookIoGate *p_ioGate, AutoSavePolicy p_autoSavePolicy,
                             QObject *p_parent)
    : BufferCoreService(p_context, p_parent), m_hookMgr(p_hookMgr), m_ioGate(p_ioGate),
      m_autoSavePolicy(p_autoSavePolicy) {
  Q_ASSERT(m_hookMgr);
  Q_ASSERT(m_ioGate);

  m_autoSaveTimer = new QTimer(this);
  m_autoSaveTimer->setInterval(c_autoSaveIntervalMs);
  QObject::connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() { onAutoSaveTimerTick(); });

  // T7: BufferSaveQueue dispatches auto-save off the UI thread, serializing
  // per (notebookId, bufferId) and acquiring NotebookIoGate per notebook.
  m_saveQueue = new BufferSaveQueue(*this, *m_ioGate, this);
  QObject::connect(
      m_saveQueue, &BufferSaveQueue::saveFinished, asQObject(),
      [this](const QString &p_bufferId, quint64 p_revision, bool p_ok, const QString &p_errorMsg) {
        onSaveFinished(p_bufferId, p_revision, p_ok, p_errorMsg);
      },
      Qt::QueuedConnection);
  // T28: forward the read-only rejection signal to consumers via this
  // BufferService instance so UI listeners (ViewWindow2 modal warning) can
  // subscribe without reaching into the private m_saveQueue. The queue emits
  // directly on the UI thread inside enqueue() (per its threading contract),
  // so a DirectConnection is correct here — keeps the modal trigger
  // synchronous with the rejected enqueue call. NOTE: BufferService inherits
  // QObject privately (via BufferCoreService), so we route through a lambda
  // bound to asQObject() and forward by emitting via the PMF — the lambda
  // captures `this` to access the protected emit hook.
  QObject::connect(
      m_saveQueue, &BufferSaveQueue::saveRejectedReadOnly, asQObject(),
      [this](const QString &p_bufferId) { emit saveRejectedReadOnly(p_bufferId); },
      Qt::DirectConnection);
  QObject::connect(
      m_saveQueue, &BufferSaveQueue::protectedSaveFinished, asQObject(),
      [this](const QString &id, quint64 generation, quint64 revision, bool backup, int error) {
        onProtectedSaveFinished(id, generation, revision, backup, error);
      });

  // Closing a notebook drops its buffers inside vxcore without routing through
  // closeBuffer(); sweep them out of the per-buffer maps here.
  m_hookMgr->addAction<NotebookCloseEvent>(
      HookNames::NotebookAfterClose,
      [this](HookContext &, const NotebookCloseEvent &) { pruneClosedBuffers(); }, 10);
}

BufferService::BufferService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                             AutoSavePolicy p_autoSavePolicy, QObject *p_parent)
    : BufferService(p_context, p_hookMgr, new NotebookIoGate(), p_autoSavePolicy, p_parent) {
  // Re-tag the gate we just created as owned so the dtor cleans it up.
  m_ownedIoGate = m_ioGate;
}

BufferService::~BufferService() {
  // Safety net — idempotent. The aboutToQuit handler should have called this already.
  if (m_saveQueue) {
    m_saveQueue->shutdown(5000);
    m_saveQueue->drainProtected(-1);
  }
  if (m_protectedBuffers) {
    for (const auto &state : m_protectedBuffers->buffers) {
      QMutexLocker lock(&state->mutex);
      state->current.store(false, std::memory_order_release);
    }
  }
  delete m_ownedIoGate;
  m_ownedIoGate = nullptr;
}

bool BufferService::shutdown(int p_timeoutMs) {
  return m_saveQueue ? m_saveQueue->shutdown(p_timeoutMs) : true;
}

QObject *BufferService::asQObject() { return this; }

void BufferService::setAutoSavePolicy(AutoSavePolicy p_policy) { m_autoSavePolicy = p_policy; }

void BufferService::syncAutoSavePolicy(int p_configPolicy) {
  // Map EditorConfig::AutoSavePolicy (0=None, 1=AutoSave, 2=BackupFile)
  // to BufferService::AutoSavePolicy.
  AutoSavePolicy policy = AutoSavePolicy::AutoSave;
  if (p_configPolicy == 0) {
    policy = AutoSavePolicy::None;
  } else if (p_configPolicy == 2) {
    policy = AutoSavePolicy::BackupFile;
  }
  setAutoSavePolicy(policy);
}

// ============ Buffer Lifecycle (with hooks) ============

Buffer2 BufferService::openBuffer(const NodeIdentifier &p_nodeId,
                                  const FileOpenSettings &p_settings, VxCoreError *p_outErr) {
  VxCoreError openError = VXCORE_OK;
  if (!p_outErr) {
    p_outErr = &openError;
  }
  if (p_outErr) {
    *p_outErr = VXCORE_OK;
  }
  qDebug() << "BufferService::openBuffer notebook:" << p_nodeId.notebookId
           << "path:" << p_nodeId.relativePath << "mode:" << static_cast<int>(p_settings.m_mode)
           << "readOnly:" << p_settings.m_readOnly << "lineNumber:" << p_settings.m_lineNumber;

  FileOpenEvent event;
  event.notebookId = p_nodeId.notebookId;
  event.filePath = p_nodeId.relativePath;
  event.mode = static_cast<int>(p_settings.m_mode);
  event.forceMode = p_settings.m_forceMode;
  event.focus = p_settings.m_focus;
  event.newFile = p_settings.m_newFile;
  event.readOnly = p_settings.m_readOnly;
  event.lineNumber = p_settings.m_lineNumber;
  event.cursorOffset = p_settings.m_cursorOffset;
  event.anchor = p_settings.m_anchor;
  event.alwaysNewWindow = p_settings.m_alwaysNewWindow;
  event.detachedView = p_settings.m_detachedView;
  if (p_settings.m_searchHighlight.m_isValid) {
    event.searchPatterns = p_settings.m_searchHighlight.m_patterns;
    event.searchOptions = static_cast<int>(p_settings.m_searchHighlight.m_options);
    event.searchCurrentMatchLine = p_settings.m_searchHighlight.m_currentMatchLine;
  }
  if (m_hookMgr->doAction(HookNames::FileBeforeOpen, event)) {
    qDebug() << "BufferService::openBuffer cancelled by hook";
    return Buffer2(); // Cancelled by plugin.
  }

  QString bufferId;
  if (p_nodeId.relativePath.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive)) {
    if (m_protectedLocking) {
      if (p_outErr) {
        *p_outErr = VXCORE_ERR_ENCRYPTION_LOCKED;
      }
      return Buffer2();
    }
    NodeIdentifier actual = p_nodeId;
    if (actual.notebookId.isEmpty()) {
      char *notebookId = nullptr;
      char *relativePath = nullptr;
      const auto error = vxcore_path_resolve(m_context, actual.relativePath.toUtf8().constData(),
                                             &notebookId, &relativePath);
      if (error != VXCORE_OK) {
        if (p_outErr) {
          *p_outErr = error;
        }
        return Buffer2();
      }
      actual.notebookId = cstrToQString(notebookId);
      actual.relativePath = cstrToQString(relativePath);
    }
    event.notebookId = actual.notebookId;
    event.filePath = actual.relativePath;
    NotebookIoGate::ScopedTryLock gate(*m_ioGate, actual.notebookId, 50);
    if (!gate.isLocked()) {
      if (p_outErr) {
        *p_outErr = VXCORE_ERR_SYNC_IN_PROGRESS;
      }
      return Buffer2();
    }
    bufferId = BufferCoreService::openBuffer(actual.notebookId, actual.relativePath, p_outErr);
  } else {
    bufferId = BufferCoreService::openBuffer(p_nodeId.notebookId, p_nodeId.relativePath, p_outErr);
  }

  if (bufferId.isEmpty()) {
    if (*p_outErr == VXCORE_ERR_ENCRYPTION_LOCKED && !m_protectedLocking) {
      emit protectedOpenRequested({event.notebookId, event.filePath}, p_settings);
      return Buffer2();
    }
    qWarning() << "BufferService::openBuffer failed for" << p_nodeId.relativePath;
    return Buffer2(); // Failed to open.
  }

  // Resolve the buffer's read-only state ONCE, BEFORE firing FileAfterOpen.
  // ViewAreaController creates the ViewWindow2 synchronously inside that hook
  // and reads Buffer2::isReadOnly() to decide whether the editor is editable,
  // so resolving afterwards would hand out a writable editor for a read-only
  // buffer. The per-open override and the notebook's own read-only flag are
  // ORed here; from now on nothing else needs to know which one applied.
  //
  // vxcore dedups buffers by path, so re-opening an already-open file returns
  // the same id and the FIRST resolution wins: once read-only, the shared
  // buffer stays read-only until it is closed (fail-closed on purpose).
  const bool alreadyKnown = m_bufferFlags.contains(bufferId);
  if (!alreadyKnown) {
    resolveBufferFacts(bufferId, p_settings.m_readOnly, BufferCoreService::getBuffer(bufferId));
  } else if (p_settings.m_readOnly) {
    m_bufferFlags[bufferId] |= ReadOnly;
    if ((m_bufferFlags.value(bufferId) & Encrypted) != 0) {
      const auto state = m_protectedBuffers->buffers.value(bufferId);
      state->readOnly.store(true, std::memory_order_release);
    }
  }
  const bool encrypted = (m_bufferFlags.value(bufferId) & Encrypted) != 0;
  if (encrypted) {
    // A restored inactive handle was classified without loading. The explicit
    // core open above authenticated it; adopt that authenticated editor fact.
    if (alreadyKnown) {
      resolveBufferFacts(bufferId, p_settings.m_readOnly, BufferCoreService::getBuffer(bufferId));
    }
    const auto buffer = protectedHandle(bufferId);
    if (!buffer.m_protectedState->current.load(std::memory_order_acquire) ||
        buffer.m_protectedState->editorType.isEmpty()) {
      if (p_outErr) {
        *p_outErr = VXCORE_ERR_INVALID_STATE;
      }
      return Buffer2();
    }
  }
  if (encrypted && m_protectedLocking) {
    if (p_outErr) {
      *p_outErr = VXCORE_ERR_ENCRYPTION_LOCKED;
    }
    return Buffer2();
  }

  event.bufferId = bufferId;
  if (encrypted) {
    const auto buffer = protectedHandle(bufferId);
    event.notebookId = buffer.nodeId().notebookId;
    event.filePath = buffer.nodeId().relativePath;
  }
  m_hookMgr->doAction(HookNames::FileAfterOpen, event);

  qDebug() << "BufferService::openBuffer succeeded bufferId:" << bufferId;
  return encrypted ? protectedHandle(bufferId) : Buffer2(this, m_hookMgr, bufferId, p_nodeId);
}

Buffer2 BufferService::openBufferByNodeId(const QString &p_nodeId,
                                          const FileOpenSettings &p_settings) {
  // Step 1: Resolve UUID to notebookId + relativePath (NO buffer opened here).
  QString notebookId;
  QString relativePath;
  if (!BufferCoreService::resolveNodeId(p_nodeId, notebookId, relativePath)) {
    qDebug() << "BufferService::openBufferByNodeId: UUID not found:" << p_nodeId;
    return Buffer2();
  }

  // Step 2: Delegate to existing openBuffer() which fires FileBeforeOpen BEFORE
  // vxcore_buffer_open — preserving the hook-before-open contract.
  NodeIdentifier nodeId;
  nodeId.notebookId = notebookId;
  nodeId.relativePath = relativePath;
  return openBuffer(nodeId, p_settings);
}

Buffer2 BufferService::openVirtualBuffer(const QString &p_address) {
  qDebug() << "BufferService::openVirtualBuffer address:" << p_address;

  // Call vxcore to open (or dedup) the virtual buffer.
  QString bufferId = BufferCoreService::openVirtualBuffer(p_address);

  if (bufferId.isEmpty()) {
    qWarning() << "BufferService::openVirtualBuffer failed for" << p_address;
    return Buffer2();
  }

  // Track as virtual for auto-save skip.
  m_virtualBufferIds.insert(bufferId);

  // Construct NodeIdentifier with empty notebookId and address as path.
  NodeIdentifier nodeId;
  nodeId.notebookId = QString();
  nodeId.relativePath = p_address;

  // NO hooks fired — virtual buffers are not files.

  qDebug() << "BufferService::openVirtualBuffer succeeded bufferId:" << bufferId;
  return Buffer2(this, m_hookMgr, bufferId, nodeId);
}

bool BufferService::closeBuffer(const QString &p_bufferId) {
  if ((m_bufferFlags.value(p_bufferId) & Encrypted) != 0) {
    const auto buffer = protectedHandle(p_bufferId);
    if (!buffer.isValid()) {
      return false;
    }
    const auto state = buffer.m_protectedState;
    QMutexLocker lock(&state->mutex);
    if (state->operations != 0) {
      return false;
    }
    // Prevent a worker from acquiring a lease between the check and close.
    state->current.store(false, std::memory_order_release);
    if (!BufferCoreService::closeBuffer(p_bufferId)) {
      state->current.store(true, std::memory_order_release);
      return false;
    }
    lock.unlock();
    discardBufferState(p_bufferId);
    return true;
  }
  discardBufferState(p_bufferId);
  return BufferCoreService::closeBuffer(p_bufferId);
}

void BufferService::discardBufferState(const QString &p_bufferId) {
  // Clean up all per-buffer transient state.
  m_dirtyBuffers.remove(p_bufferId);
  m_activeWriters.remove(p_bufferId);
  m_saveFailureCounts.remove(p_bufferId);
  m_virtualBufferIds.remove(p_bufferId);
  m_bufferFlags.remove(p_bufferId);
  if (m_protectedBuffers) {
    const auto state = m_protectedBuffers->buffers.take(p_bufferId);
    if (state) {
      QMutexLocker lock(&state->mutex);
      state->current.store(false, std::memory_order_release);
    }
  }
  m_revisions.remove(p_bufferId);
  m_bufferEncodings.remove(p_bufferId);
  if (m_dirtyBuffers.isEmpty()) {
    m_autoSaveTimer->stop();
  }
}

bool BufferService::forgetBufferIfClosed(const QString &p_bufferId) {
  if (p_bufferId.isEmpty()) {
    return false;
  }
  // vxcore auto-closes a buffer once it is removed from its last workspace, so
  // the ordinary tab-close path never reaches closeBuffer() above. Ask vxcore
  // whether the buffer still exists (listBuffers, not getBuffer — the latter
  // logs a warning for an unknown id, which is the expected case here) and drop
  // the Qt-side state if it is gone. Without this every per-buffer map,
  // including the resolved read-only fact, would grow for the whole session.
  const QJsonArray buffers = BufferCoreService::listBuffers();
  for (const auto &v : buffers) {
    if (v.toObject().value(QStringLiteral("id")).toString() == p_bufferId) {
      return false; // Still open elsewhere.
    }
  }

  discardBufferState(p_bufferId);
  return true;
}

int BufferService::pruneClosedBuffers() {
  // Sweep for buffers vxcore closed without telling us. Closing a notebook
  // drops all of its buffers inside vxcore (CloseBuffersForNotebook), which
  // reaches neither closeBuffer() nor the per-tab forgetBufferIfClosed() hook.
  // vxcore has no buffer-closed event, so a sweep is the available mechanism.
  QSet<QString> live;
  const QJsonArray buffers = BufferCoreService::listBuffers();
  for (const auto &v : buffers) {
    const QString id = v.toObject().value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
      live.insert(id);
    }
  }

  // Snapshot the existing resolved-facts roster: discardBufferState mutates it.
  const QList<QString> tracked = m_bufferFlags.keys();
  int dropped = 0;
  for (const QString &id : tracked) {
    if (!live.contains(id)) {
      discardBufferState(id);
      ++dropped;
    }
  }
  return dropped;
}

// ============ Per-Buffer Encoding ============

QString BufferService::bufferEncoding(const QString &p_bufferId) const {
  return m_bufferEncodings.value(p_bufferId, QStringLiteral("UTF-8"));
}

void BufferService::setBufferEncoding(const QString &p_bufferId, const QString &p_codecName) {
  if (p_codecName.isEmpty()) {
    m_bufferEncodings.remove(p_bufferId);
  } else {
    m_bufferEncodings.insert(p_bufferId, p_codecName);
  }
}

QByteArray BufferService::encodeContent(const QString &p_bufferId, const QString &p_text) const {
  // Fast path: no override → plain UTF-8, identical to the pre-encoding code
  // path (no codec-registry lookup, no virtual dispatch).
  auto it = m_bufferEncodings.constFind(p_bufferId);
  if (it == m_bufferEncodings.constEnd()) {
    return p_text.toUtf8();
  }
  return resolveCodec(it.value())->fromUnicode(p_text);
}

QString BufferService::decodeContent(const QString &p_bufferId,
                                     const QByteArrayViewCompat &p_raw) const {
  auto it = m_bufferEncodings.constFind(p_bufferId);
  if (it == m_bufferEncodings.constEnd()) {
    return QString::fromUtf8(p_raw);
  }
  return resolveCodec(it.value())->toUnicode(p_raw.data(), static_cast<int>(p_raw.size()));
}

// ============ Buffer Handle ============

Buffer2 BufferService::getBufferHandle(const QString &p_bufferId) {
  if (p_bufferId.isEmpty()) {
    return Buffer2();
  }

  // Query vxcore for the buffer info to construct NodeIdentifier.
  QJsonObject bufJson = BufferCoreService::getBuffer(p_bufferId);
  if (bufJson.isEmpty()) {
    qWarning() << "BufferService::getBufferHandle: buffer not found for" << p_bufferId;
    return Buffer2();
  }

  NodeIdentifier nodeId;
  nodeId.notebookId = bufJson.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
  nodeId.relativePath = bufJson.value(QStringLiteral("filePath")).toString();

  // Adoption: a buffer restored from the session was created inside vxcore
  // before this service existed, so it never went through openBuffer. Resolve
  // its read-only state here — BEFORE the handle escapes — or the restored
  // ViewWindow2 would be built writable for a read-only notebook.
  resolveBufferFacts(p_bufferId, false, bufJson);

  return (m_bufferFlags.value(p_bufferId) & Encrypted) != 0
             ? protectedHandle(p_bufferId)
             : Buffer2(this, m_hookMgr, p_bufferId, nodeId);
}

void BufferService::resolveBufferFacts(const QString &p_bufferId, bool p_forcedReadOnly,
                                       const QJsonObject &p_bufferInfo) {
  auto it = m_bufferFlags.find(p_bufferId);
  if (it == m_bufferFlags.end()) {
    const bool readOnly = isNotebookReadOnlyForBuffer(p_bufferId, &p_bufferInfo);
    it = m_bufferFlags.insert(p_bufferId, readOnly ? ReadOnly : 0);
  }
  if (p_forcedReadOnly) {
    it.value() |= ReadOnly;
  }
  if (!p_bufferInfo.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool()) {
    return;
  }
  it.value() |= Encrypted;
  if (!m_protectedBuffers) {
    m_protectedBuffers.reset(new ProtectedBuffers);
  }
  m_saveQueue->prepareProtected();
  NodeIdentifier nodeId;
  nodeId.notebookId = p_bufferInfo.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
  nodeId.relativePath = p_bufferInfo.value(QStringLiteral("filePath")).toString();
  const QString editorType =
      p_bufferInfo.value(QLatin1String(vxcore::kJsonKeyEditorType)).toString();
  auto &state = m_protectedBuffers->buffers[p_bufferId];
  if (state) {
    QMutexLocker lock(&state->mutex);
    state->readOnly.store((it.value() & ReadOnly) != 0, std::memory_order_release);
    if (state->current.load(std::memory_order_acquire) && state->nodeId == nodeId &&
        state->editorType == editorType) {
      return;
    }
    // Identity/editor adoption is a generation change, never a mutation of
    // the identity already captured by workers.
    state->current.store(false, std::memory_order_release);
    if (state->operations != 0) {
      return;
    }
  }
  state = std::make_shared<ProtectedBufferState>();
  state->owner = this;
  state->bufferId = p_bufferId;
  state->generation = ++m_nextProtectedGeneration;
  state->locking = m_protectedLocking;
  state->readOnly.store((it.value() & ReadOnly) != 0, std::memory_order_release);
  state->nodeId = std::move(nodeId);
  state->editorType = editorType;
}

Buffer2 BufferService::protectedHandle(const QString &p_bufferId) const {
  if (!m_protectedBuffers) {
    return Buffer2();
  }
  const auto state = m_protectedBuffers->buffers.value(p_bufferId);
  if (!state) {
    return Buffer2();
  }
  Buffer2 buffer(const_cast<BufferService *>(this), m_hookMgr, p_bufferId, state->nodeId);
  buffer.m_protectedState = state;
  return buffer;
}

Buffer2 BufferService::findOpenProtectedBuffer(const NodeIdentifier &p_nodeId) const {
  if (m_protectedBuffers) {
    for (const auto &state : m_protectedBuffers->buffers) {
      if (state->nodeId == p_nodeId && !state->editorType.isEmpty() &&
          state->current.load(std::memory_order_acquire)) {
        return protectedHandle(state->bufferId);
      }
    }
  }
  return Buffer2();
}

std::shared_ptr<ProtectedBufferLease>
BufferService::acquireProtectedLease(const Buffer2 &p_buffer, bool p_durability,
                                     VxCoreError *p_outError) const {
  auto fail = [p_outError](VxCoreError p_error) {
    if (p_outError) {
      *p_outError = p_error;
    }
    return std::shared_ptr<ProtectedBufferLease>();
  };
  const auto &state = p_buffer.m_protectedState;
  if (p_buffer.m_bufferService != this || !state || state->owner != this) {
    return fail(VXCORE_ERR_INVALID_STATE);
  }
  try {
    auto lease = std::shared_ptr<ProtectedBufferLease>(new ProtectedBufferLease(state));
    QMutexLocker lock(&state->mutex);
    if (!state->current.load(std::memory_order_acquire)) {
      return fail(VXCORE_ERR_INVALID_STATE);
    }
    if (state->editorType.isEmpty() || (state->locking && !p_durability)) {
      return fail(VXCORE_ERR_ENCRYPTION_LOCKED);
    }
    if (p_durability && state->readOnly.load(std::memory_order_acquire)) {
      return fail(VXCORE_ERR_READ_ONLY);
    }
    ++state->operations;
    lease->m_acquired = true;
    if (p_outError) {
      *p_outError = VXCORE_OK;
    }
    return lease;
  } catch (const std::bad_alloc &) {
    return fail(VXCORE_ERR_OUT_OF_MEMORY);
  }
}

QList<Buffer2> BufferService::protectedBuffers() const {
  QList<Buffer2> buffers;
  if (m_protectedBuffers) {
    for (const auto &state : m_protectedBuffers->buffers) {
      const auto buffer = protectedHandle(state->bufferId);
      if (buffer.isValid()) {
        buffers.append(buffer);
      }
    }
  }
  return buffers;
}

bool BufferService::protectedBufferOperationsIdle(const QString &p_bufferId) const {
  if (!m_protectedBuffers) {
    return true;
  }
  const auto state = m_protectedBuffers->buffers.value(p_bufferId);
  if (!state) {
    return true;
  }
  QMutexLocker lock(&state->mutex);
  return state->operations == 0;
}

bool BufferService::protectedOperationsIdle() const {
  if (m_protectedOperations != 0) {
    return false;
  }
  if (m_protectedBuffers) {
    for (const auto &state : m_protectedBuffers->buffers) {
      QMutexLocker lock(&state->mutex);
      if (state->operations != 0) {
        return false;
      }
    }
  }
  return true;
}

bool BufferService::beginProtectedOperation() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_protectedLocking) {
    return false;
  }
  ++m_protectedOperations;
  return true;
}

void BufferService::endProtectedOperation() {
  Q_ASSERT(QThread::currentThread() == thread());
  Q_ASSERT(m_protectedOperations != 0);
  --m_protectedOperations;
}

bool BufferService::beginProtectedLocking() {
  if (m_protectedLocking || m_protectedOperations != 0) {
    return false;
  }
  m_protectedLocking = true;
  if (m_protectedBuffers) {
    for (const auto &state : m_protectedBuffers->buffers) {
      QMutexLocker lock(&state->mutex);
      state->locking = true;
    }
  }
  emit protectedLockingChanged(true);
  return true;
}

void BufferService::cancelProtectedLocking() {
  if (!m_protectedLocking) {
    return;
  }
  if (m_protectedBuffers) {
    for (const auto &state : m_protectedBuffers->buffers) {
      QMutexLocker lock(&state->mutex);
      state->locking = false;
    }
  }
  m_protectedLocking = false;
  emit protectedLockingChanged(false);
}

VxCoreError
BufferService::withProtectedBuffer(const Buffer2 &p_buffer, bool p_durability,
                                   const std::function<VxCoreError()> &p_operation) const {
  VxCoreError error;
  auto lease = acquireProtectedLease(p_buffer, p_durability, &error);
  if (!lease) {
    return error;
  }
  const QString &notebookId = lease->m_state->nodeId.notebookId;
  if (QThread::currentThread() == thread()) {
    NotebookIoGate::ScopedTryLock gate(*m_ioGate, notebookId, 50);
    if (!gate.isLocked()) {
      return VXCORE_ERR_SYNC_IN_PROGRESS;
    }
    return lease->isCurrent() ? p_operation() : VXCORE_ERR_INVALID_STATE;
  }
  NotebookIoGate::ScopedLock gate(*m_ioGate, notebookId);
  return lease->isCurrent() ? p_operation() : VXCORE_ERR_INVALID_STATE;
}

VxCoreError BufferService::saveProtectedSnapshot(const Buffer2 &p_buffer, QByteArray p_content,
                                                 quint64 p_revision, int p_gateTimeoutMs) {
  struct Wipe {
    QByteArray &bytes;
    ~Wipe() {
      volatile char *data = bytes.data();
      for (int i = 0; i < bytes.size(); ++i)
        data[i] = 0;
    }
  } wipe{p_content};
  VxCoreError error;
  auto lease = acquireProtectedLease(p_buffer, true, &error);
  if (!lease)
    return error;
  if (QThread::currentThread() != thread())
    return VXCORE_ERR_INVALID_STATE;
  if (m_saveQueue->isProtectedBusy(p_buffer.m_bufferId))
    return VXCORE_ERR_INVALID_STATE;

  // The FIFO owns the raw snapshot and performs every filesystem write on its
  // worker. Wait for its actual result while allowing paint/queued completions.
  QEventLoop loop;
  struct Completion {
    VxCoreError error = VXCORE_ERR_INVALID_STATE;
    bool completed = false;
    QPointer<QEventLoop> loop;
  };
  auto completion = std::make_shared<Completion>();
  completion->loop = &loop;
  const auto finished = [completion](int result) {
    completion->error = static_cast<VxCoreError>(result);
    completion->completed = true;
    if (completion->loop)
      completion->loop->quit();
  };
  if (!m_saveQueue->enqueueProtected(*this, p_buffer.nodeId().notebookId, lease, QString(),
                                     p_revision, QString(), false, &p_content, finished,
                                     p_gateTimeoutMs)) {
    return VXCORE_ERR_INVALID_STATE;
  }
  p_buffer.m_protectedState->lastQueuedRevision = p_revision;
  if (!completion->completed)
    loop.exec(QEventLoop::ExcludeUserInputEvents);
  if (!completion->completed)
    return VXCORE_ERR_CANCELLED;
  return lease->isCurrent() ? completion->error : VXCORE_ERR_INVALID_STATE;
}

QByteArray BufferService::readResource(const Buffer2 &p_buffer, const QString &p_resourceUrl,
                                       VxCoreError *p_error) const {
  return BufferCoreService::readResource(p_buffer.m_bufferId, p_resourceUrl, p_error);
}

VxCoreError BufferService::exportResource(const Buffer2 &p_buffer, const QString &p_resourceUrl,
                                          const QString &p_destination) const {
  return BufferCoreService::exportResource(p_buffer.m_bufferId, p_resourceUrl, p_destination);
}

QJsonObject BufferService::getContent(const Buffer2 &p_buffer, VxCoreError *p_error) const {
  if (!p_buffer.isEncrypted()) {
    return BufferCoreService::getContent(p_buffer.m_bufferId, p_error);
  }
  QJsonObject result;
  const auto error = withProtectedBuffer(p_buffer, false, [&]() {
    VxCoreError status;
    result = BufferCoreService::getContent(p_buffer.m_bufferId, &status);
    return status;
  });
  if (p_error) {
    *p_error = error;
  }
  return result;
}

QByteArray BufferService::getContentRaw(const Buffer2 &p_buffer, VxCoreError *p_error) const {
  if (!p_buffer.isEncrypted()) {
    return BufferCoreService::getContentRaw(p_buffer.m_bufferId, p_error);
  }
  QByteArray result;
  const auto error = withProtectedBuffer(p_buffer, false, [&]() {
    VxCoreError status;
    result = BufferCoreService::getContentRaw(p_buffer.m_bufferId, &status);
    return status;
  });
  if (p_error) {
    *p_error = error;
  }
  return result;
}

QByteArrayViewCompat BufferService::peekContentRaw(const Buffer2 &p_buffer,
                                                   VxCoreError *p_error) const {
  if (!p_buffer.isEncrypted()) {
    return BufferCoreService::peekContentRaw(p_buffer.m_bufferId, p_error);
  }
  QByteArrayViewCompat result;
  const auto error = withProtectedBuffer(p_buffer, false, [&]() {
    // A borrowed view cannot outlive a concurrently mutating worker. Ordinary
    // callers keep their existing pointer path; protected callers retry when idle.
    if (m_saveQueue->isProtectedBusy(p_buffer.m_bufferId)) {
      return VXCORE_ERR_INVALID_STATE;
    }
    VxCoreError status;
    result = BufferCoreService::peekContentRaw(p_buffer.m_bufferId, &status);
    return status;
  });
  if (p_error) {
    *p_error = error;
  }
  return result;
}

bool BufferService::setContent(const Buffer2 &p_buffer, const QString &p_contentJson) {
  if (!p_buffer.isEncrypted()) {
    return BufferCoreService::setContent(p_buffer.m_bufferId, p_contentJson);
  }
  if (p_buffer.m_protectedState->readOnly.load(std::memory_order_acquire)) {
    return false;
  }
  return withProtectedBuffer(p_buffer, false, [&]() {
           return BufferCoreService::setContent(p_buffer.m_bufferId, p_contentJson)
                      ? VXCORE_OK
                      : VXCORE_ERR_INVALID_STATE;
         }) == VXCORE_OK;
}

bool BufferService::setContentRaw(const Buffer2 &p_buffer, const QByteArray &p_data) {
  if (!p_buffer.isEncrypted()) {
    return BufferCoreService::setContentRaw(p_buffer.m_bufferId, p_data);
  }
  if (p_buffer.m_protectedState->readOnly.load(std::memory_order_acquire)) {
    return false;
  }
  return withProtectedBuffer(p_buffer, false, [&]() {
           VxCoreError error;
           BufferCoreService::setContentRaw(p_buffer.m_bufferId, p_data, &error);
           return error;
         }) == VXCORE_OK;
}

QString BufferService::insertAsset(const Buffer2 &p_buffer, const QString &p_sourcePath) {
  if (p_buffer.isReadOnly()) {
    return QString();
  }
  return BufferCoreService::insertAsset(p_buffer.m_bufferId, p_sourcePath);
}

QString BufferService::insertAssetRaw(const Buffer2 &p_buffer, const QString &p_assetName,
                                      const QByteArray &p_data) {
  if (p_buffer.isReadOnly()) {
    return QString();
  }
  return BufferCoreService::insertAssetRaw(p_buffer.m_bufferId, p_assetName, p_data);
}

bool BufferService::deleteAsset(const Buffer2 &p_buffer, const QString &p_relativePath) {
  if (p_buffer.isReadOnly()) {
    return false;
  }
  return BufferCoreService::deleteAsset(p_buffer.m_bufferId, p_relativePath);
}

QJsonArray BufferService::listAttachments(const Buffer2 &p_buffer) const {
  return BufferCoreService::listAttachments(p_buffer.m_bufferId);
}

bool BufferService::checkExternalChanges(const Buffer2 &p_buffer) {
  if (!p_buffer.isEncrypted()) {
    return BufferCoreService::checkExternalChanges(p_buffer.m_bufferId);
  }
  return withProtectedBuffer(p_buffer, false, [&]() {
           return BufferCoreService::checkExternalChanges(p_buffer.m_bufferId) ? VXCORE_OK
                                                                               : VXCORE_ERR_IO;
         }) == VXCORE_OK;
}

// ============ Pass-through methods ============

bool BufferService::isNotebookBundled(const QString &p_notebookId) const {
  if (p_notebookId.isEmpty()) {
    return false;
  }
  char *json = nullptr;
  VxCoreError err = vxcore_notebook_get_config(m_context, p_notebookId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    return false;
  }
  QJsonObject config = parseJsonObjectFromCStr(json);
  return config.value(QLatin1String(vxcore::kJsonKeyType))
             .toString()
             .compare(QStringLiteral("bundled"), Qt::CaseInsensitive) == 0;
}

QJsonObject BufferService::getBuffer(const QString &p_bufferId) const {
  return BufferCoreService::getBuffer(p_bufferId);
}

bool BufferService::isVirtualBuffer(const QString &p_bufferId) const {
  return m_virtualBufferIds.contains(p_bufferId);
}

QJsonArray BufferService::listBuffers() const { return BufferCoreService::listBuffers(); }

bool BufferService::isBufferReadOnly(const QString &p_bufferId) const {
  // Resolved once at open time; see openBuffer.
  return (m_bufferFlags.value(p_bufferId) & (ReadOnly | Converting)) != 0;
}

// ============ BufferCoreService wrappers ============

bool BufferService::saveBuffer(const QString &p_bufferId) {
  const auto flags = m_bufferFlags.value(p_bufferId);
  if ((flags & Encrypted) != 0) {
    return saveBuffer(protectedHandle(p_bufferId));
  }
  // Read-only buffers must never reach vxcore_buffer_save (which returns
  // "Notebook is read-only" and surfaces a generic save failure). Mirrors the
  // markDirty / BufferSaveQueue::enqueue guards. UI consumes saveRejectedReadOnly
  // via ViewWindow2::showReadOnlyWarning.
  if ((flags & ReadOnly) != 0) {
    qWarning() << "BufferService::saveBuffer rejected: buffer is read-only" << p_bufferId;
    emit saveRejectedReadOnly(p_bufferId);
    return false;
  }
  bool ok = BufferCoreService::saveBuffer(p_bufferId);
  emit bufferModifiedChanged(p_bufferId);
  return ok;
}

bool BufferService::saveBuffer(const Buffer2 &p_buffer, VxCoreError *p_error) {
  if (isBufferReadOnly(p_buffer.m_bufferId)) {
    if (p_error) {
      *p_error = VXCORE_ERR_READ_ONLY;
    }
    emit saveRejectedReadOnly(p_buffer.m_bufferId);
    return false;
  }
  if (!p_buffer.isEncrypted()) {
    const bool ok = BufferCoreService::saveBuffer(p_buffer.m_bufferId, p_error);
    emit bufferModifiedChanged(p_buffer.m_bufferId);
    return ok;
  }
  VxCoreError error;
  if (QThread::currentThread() == thread()) {
    const auto revision = currentRevision(p_buffer.m_bufferId);
    QByteArray snapshot;
    error = withProtectedBuffer(p_buffer, true, [&]() {
      if (m_saveQueue->isProtectedBusy(p_buffer.m_bufferId))
        return VXCORE_ERR_INVALID_STATE;
      VxCoreError status;
      snapshot = BufferCoreService::getContentRaw(p_buffer.m_bufferId, &status);
      return status;
    });
    if (error == VXCORE_OK) {
      error = saveProtectedSnapshot(p_buffer, std::move(snapshot), revision, 50);
      if (error == VXCORE_OK && currentRevision(p_buffer.m_bufferId) != revision) {
        error = VXCORE_ERR_FILE_CHANGED_OUTSIDE;
      }
    }
  } else {
    error = withProtectedBuffer(p_buffer, true, [&]() {
      if (m_saveQueue->isProtectedBusy(p_buffer.m_bufferId))
        return VXCORE_ERR_INVALID_STATE;
      VxCoreError status;
      BufferCoreService::saveBuffer(p_buffer.m_bufferId, &status);
      return status;
    });
  }
  if (p_error) {
    *p_error = error;
  }
  emit bufferModifiedChanged(p_buffer.m_bufferId);
  return error == VXCORE_OK;
}

bool BufferService::reloadBuffer(const Buffer2 &p_buffer, VxCoreError *p_error) {
  const QString &p_bufferId = p_buffer.m_bufferId;
  if (p_error) {
    *p_error = VXCORE_ERR_CANCELLED;
  }
  // Fire FileBeforeReload hook (cancellable).
  BufferEvent event;
  event.bufferId = p_bufferId;
  if (m_hookMgr->doAction(HookNames::FileBeforeReload, event)) {
    return false; // Cancelled by plugin.
  }

  bool ok;
  if (p_buffer.isEncrypted()) {
    const auto error = withProtectedBuffer(p_buffer, false, [&]() {
      if (m_saveQueue->isProtectedBusy(p_bufferId)) {
        return VXCORE_ERR_INVALID_STATE;
      }
      VxCoreError status;
      BufferCoreService::reloadBuffer(p_bufferId, &status);
      return status;
    });
    if (p_error) {
      *p_error = error;
    }
    ok = error == VXCORE_OK;
  } else {
    ok = BufferCoreService::reloadBuffer(p_bufferId, p_error);
  }

  if (ok) {
    // Fire FileAfterReload hook (informational).
    m_hookMgr->doAction(HookNames::FileAfterReload, event);
  }

  emit bufferModifiedChanged(p_bufferId);
  return ok;
}

QStringList BufferService::checkAllExternalChanges() {
  QStringList changedBufferIds;
  QJsonArray buffers = BufferCoreService::listBuffers();
  for (const auto &bufferVal : buffers) {
    QJsonObject bufObj = bufferVal.toObject();
    QString bufferId = bufObj.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    if (bufferId.isEmpty()) {
      continue;
    }

    // Skip virtual buffers.
    bool isVirtual = bufObj.value(QStringLiteral("isVirtual")).toBool();
    if (isVirtual) {
      continue;
    }
    const bool encrypted = bufObj.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool();

    // Skip while VNote is itself writing this buffer's file on a worker thread
    // (per-buffer gate applied to the full sweep so background tabs do not
    // false-positive against their own in-flight save). See
    // checkSingleExternalChange for the rationale.
    if (m_saveQueue) {
      const QString notebookId = bufObj.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
      if (encrypted ? m_saveQueue->isProtectedBusy(bufferId)
                    : m_saveQueue->isBusy(notebookId, bufferId)) {
        continue;
      }
    }

    // Check for external changes.
    if (!(encrypted ? checkExternalChanges(protectedHandle(bufferId))
                    : BufferCoreService::checkExternalChanges(bufferId))) {
      continue;
    }

    // Query the updated state.
    BufferState state = BufferCoreService::getState(bufferId);
    if (state == BufferState::FileChanged || state == BufferState::FileMissing) {
      // Fire FileExternalChange hook.
      FileExternalChangeEvent event;
      event.bufferId = bufferId;
      event.filePath = bufObj.value(QStringLiteral("filePath")).toString();
      event.state = static_cast<int>(state);
      m_hookMgr->doAction(HookNames::FileExternalChange, event);

      // Emit signal for UI layer.
      emit bufferExternallyChanged(bufferId, state);
      changedBufferIds.append(bufferId);
    }
  }

  return changedBufferIds;
}

bool BufferService::checkSingleExternalChange(const QString &p_bufferId) {
  if (p_bufferId.isEmpty()) {
    return false;
  }

  // Skip virtual buffers.
  if (isVirtualBuffer(p_bufferId)) {
    return false;
  }

  // Skip while VNote is itself writing this buffer's file on a worker thread.
  // A check that lands mid-self-write would read the new on-disk mtime before
  // BufferSaveQueue's worker re-stamps Buffer::last_modified_time_, producing a
  // false-positive "modified outside VNote". Once the save drains, the stamp
  // matches disk and a later check stays NORMAL.
  bool encrypted = false;
  if (m_saveQueue) {
    const auto info = getBuffer(p_bufferId);
    encrypted = info.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool();
    const QString notebookId = info.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
    if (encrypted ? m_saveQueue->isProtectedBusy(p_bufferId)
                  : m_saveQueue->isBusy(notebookId, p_bufferId)) {
      return false;
    }
  }

  // Check for external changes via vxcore.
  if (!(encrypted ? checkExternalChanges(protectedHandle(p_bufferId))
                  : BufferCoreService::checkExternalChanges(p_bufferId))) {
    return false;
  }

  // Query the updated state.
  BufferState state = BufferCoreService::getState(p_bufferId);
  if (state == BufferState::FileChanged || state == BufferState::FileMissing) {
    // Fire FileExternalChange hook.
    FileExternalChangeEvent event;
    event.bufferId = p_bufferId;
    QJsonObject bufObj = BufferCoreService::getBuffer(p_bufferId);
    event.filePath = bufObj.value(QStringLiteral("filePath")).toString();
    event.state = static_cast<int>(state);
    m_hookMgr->doAction(HookNames::FileExternalChange, event);

    // Emit signal for UI layer.
    emit bufferExternallyChanged(p_bufferId, state);
    return true;
  }

  return false;
}

QString BufferService::insertAttachment(const Buffer2 &p_buffer, const QString &p_sourcePath) {
  if (p_buffer.isReadOnly()) {
    return QString();
  }
  const QString &p_bufferId = p_buffer.m_bufferId;
  AttachmentAddEvent event;
  event.bufferId = p_bufferId;
  event.sourcePath = p_sourcePath;
  if (m_hookMgr->doAction(HookNames::AttachmentBeforeAdd, event)) {
    return QString(); // Cancelled by plugin.
  }

  const auto filename = BufferCoreService::insertAttachment(p_bufferId, p_sourcePath);

  if (!filename.isEmpty()) {
    event.filename = filename;
    m_hookMgr->doAction(HookNames::AttachmentAfterAdd, event);
    emit attachmentChanged(p_bufferId);
  }

  return filename;
}

bool BufferService::registerAttachment(const QString &p_bufferId, const QString &p_filename) {
  const auto buffer = getBufferHandle(p_bufferId);
  if (!buffer.isValid() || !buffer.isAttachmentSupported() || buffer.isReadOnly()) {
    qWarning() << "registerAttachment failed: invalid, unsupported or read-only buffer"
               << p_bufferId;
    return false;
  }
  const auto nodeId = buffer.nodeId();
  const auto path = getExistingAttachmentPath(nodeId.notebookId, nodeId.relativePath, p_filename);
  if (path.isEmpty()) {
    return false;
  }

  AttachmentAddEvent event;
  event.bufferId = p_bufferId;
  event.sourcePath = path;
  if (m_hookMgr->doAction(HookNames::AttachmentBeforeAdd, event)) {
    return false; // Cancelled by plugin.
  }

  const auto current = getBufferHandle(p_bufferId);
  if (!current.isValid() || current.isReadOnly() || current.nodeId() != nodeId) {
    qWarning() << "registerAttachment failed: buffer changed during hook" << p_bufferId;
    return false;
  }
  {
    NotebookIoGate::ScopedTryLock lock(*m_ioGate, nodeId.notebookId, 0);
    if (!lock.isLocked()) {
      qWarning() << "registerAttachment: notebook is busy";
      return false;
    }
    if (!BufferCoreService::registerAttachment(p_bufferId, p_filename)) {
      return false;
    }
  }

  event.filename = p_filename;
  m_hookMgr->doAction(HookNames::AttachmentAfterAdd, event);
  emit attachmentChanged(p_bufferId);
  return true;
}

bool BufferService::deleteAttachment(const Buffer2 &p_buffer, const QString &p_filename) {
  const QString &p_bufferId = p_buffer.m_bufferId;
  if (p_buffer.isReadOnly()) {
    return false;
  }
  AttachmentDeleteEvent event;
  event.bufferId = p_bufferId;
  event.filename = p_filename;
  if (m_hookMgr->doAction(HookNames::AttachmentBeforeDelete, event)) {
    return false; // Cancelled by plugin.
  }

  const bool ok = BufferCoreService::deleteAttachment(p_bufferId, p_filename);

  if (ok) {
    m_hookMgr->doAction(HookNames::AttachmentAfterDelete, event);
    emit attachmentChanged(p_bufferId);
  }

  return ok;
}

QString BufferService::renameAttachment(const Buffer2 &p_buffer, const QString &p_oldFilename,
                                        const QString &p_newFilename) {
  if (p_buffer.isReadOnly()) {
    return QString();
  }
  const QString &p_bufferId = p_buffer.m_bufferId;
  AttachmentRenameEvent event;
  event.bufferId = p_bufferId;
  event.oldFilename = p_oldFilename;
  event.newFilename = p_newFilename;
  if (m_hookMgr->doAction(HookNames::AttachmentBeforeRename, event)) {
    return QString(); // Cancelled by plugin.
  }

  const auto actualName =
      BufferCoreService::renameAttachment(p_bufferId, p_oldFilename, p_newFilename);

  if (!actualName.isEmpty()) {
    event.newFilename = actualName;
    m_hookMgr->doAction(HookNames::AttachmentAfterRename, event);
    emit attachmentChanged(p_bufferId);
  }

  return actualName;
}

// ============ Auto-Save & Dirty Tracking ============

void BufferService::markDirty(const QString &p_bufferId) {
  if (p_bufferId.isEmpty()) {
    return;
  }

  // Guard: read-only buffers must never enter the dirty set (T16).
  // Notebook RO state is immutable for the notebook's lifetime under the
  // current design (no live RO transition), so checking once at the entry
  // point is sufficient. Emits dirtyRejectedReadOnly so the UI orchestration
  // layer (T28 modal warning) can warn the user. Does NOT clear an existing
  // dirty flag — under the no-live-transition rule a buffer cannot legally
  // be dirty before becoming read-only, but if it ever were, we refuse to
  // silently drop the unsaved edit.
  const auto flags = m_bufferFlags.value(p_bufferId);
  if ((flags & ReadOnly) != 0) {
    qWarning() << "BufferService::markDirty rejected: buffer is read-only" << p_bufferId;
    emit dirtyRejectedReadOnly(p_bufferId);
    return;
  }
  if ((flags & Encrypted) != 0 && m_protectedLocking) {
    emit saveError(p_bufferId,
                   QString::fromUtf8(vxcore_error_message(VXCORE_ERR_ENCRYPTION_LOCKED)));
    return;
  }

  m_dirtyBuffers.insert(p_bufferId);
  // Atomic with the auto-save trigger: bump the revision so the snapshot
  // captured by the next save tick is uniquely identifiable.
  ++m_revisions[p_bufferId].m_revision;
  if (!m_autoSaveTimer->isActive()) {
    m_autoSaveTimer->start();
  }
}

// ============ Snapshot Revision Tracking (T6) ============

quint64 BufferService::currentRevision(const QString &p_bufferId) const {
  auto it = m_revisions.constFind(p_bufferId);
  return it == m_revisions.constEnd() ? 0 : it->m_revision;
}

quint64 BufferService::lastSavedRevision(const QString &p_bufferId) const {
  auto it = m_revisions.constFind(p_bufferId);
  return it == m_revisions.constEnd() ? 0 : it->m_lastSavedRevision;
}

void BufferService::markRevisionSaved(const QString &p_bufferId, quint64 p_revision) {
  if (p_bufferId.isEmpty()) {
    return;
  }
  auto it = m_revisions.find(p_bufferId);
  if (it == m_revisions.end()) {
    return;
  }
  // Stale completion: a newer save already advanced lastSavedRevision past this.
  if (p_revision <= it->m_lastSavedRevision) {
    return;
  }
  it->m_lastSavedRevision = p_revision;
  // Only clear dirty if the latest persisted revision matches the latest edit.
  // Otherwise newer edits exist (rev > p_revision) and the buffer stays dirty.
  if (it->m_lastSavedRevision == it->m_revision) {
    m_dirtyBuffers.remove(p_bufferId);
    if (m_dirtyBuffers.isEmpty()) {
      m_autoSaveTimer->stop();
    }
  }
}

bool BufferService::isDirty(const QString &p_bufferId) const {
  return m_dirtyBuffers.contains(p_bufferId);
}

bool BufferService::isSaveQueueBusy(const QString &p_bufferId) const {
  if (p_bufferId.isEmpty() || !m_saveQueue) {
    return false;
  }

  // Use protection from the same metadata fetch already needed for the gate key.
  const auto info = getBuffer(p_bufferId);
  if (info.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool()) {
    return m_saveQueue->isProtectedBusy(p_bufferId);
  }
  const QString notebookId = info.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
  return m_saveQueue->isBusy(notebookId, p_bufferId);
}

void BufferService::syncNow(const QString &p_bufferId) {
  if (!m_dirtyBuffers.contains(p_bufferId)) {
    return;
  }

  // Virtual buffers have no file content to sync.
  if (m_virtualBufferIds.contains(p_bufferId)) {
    m_dirtyBuffers.remove(p_bufferId);
    if (m_dirtyBuffers.isEmpty()) {
      m_autoSaveTimer->stop();
    }
    return;
  }

  if (executeSyncForBuffer(p_bufferId)) {
    m_dirtyBuffers.remove(p_bufferId);
  }

  if (m_dirtyBuffers.isEmpty()) {
    m_autoSaveTimer->stop();
  }
}

bool BufferService::pullActiveWriterContent(const QString &p_bufferId) {
  if (p_bufferId.isEmpty() || m_virtualBufferIds.contains(p_bufferId)) {
    return true;
  }

  auto it = m_activeWriters.find(p_bufferId);
  if (it == m_activeWriters.end() || !it->callback) {
    // No editor attached: the vxcore buffer already holds the latest text.
    return true;
  }

  const QString content = it->callback();
  if (it->encrypted) {
    const auto buffer = protectedHandle(p_bufferId);
    const auto error = withProtectedBuffer(buffer, true, [&]() {
      if (m_saveQueue->isProtectedBusy(p_bufferId)) {
        return VXCORE_ERR_INVALID_STATE;
      }
      VxCoreError status;
      BufferCoreService::setContentRaw(p_bufferId, encodeContent(p_bufferId, content), &status);
      return status;
    });
    if (error != VXCORE_OK) {
      return false;
    }
    emit bufferContentSynced(p_bufferId);
    emit bufferModifiedChanged(p_bufferId);
    return true;
  }
  if (!BufferCoreService::setContentRaw(p_bufferId, encodeContent(p_bufferId, content))) {
    qWarning() << "BufferService::pullActiveWriterContent: setContentRaw failed for" << p_bufferId;
    return false;
  }

  emit bufferContentSynced(p_bufferId);
  emit bufferModifiedChanged(p_bufferId);
  return true;
}

bool BufferService::saveForSnapshot(const QString &p_bufferId, int p_gateTimeoutMs,
                                    QString *p_outError) {
  auto fail = [p_outError](const QString &p_message) {
    if (p_outError) {
      *p_outError = p_message;
    }
    return false;
  };

  if (p_bufferId.isEmpty()) {
    return fail(tr("The note is no longer open."));
  }
  if (m_virtualBufferIds.contains(p_bufferId)) {
    return true; // Nothing on disk to make durable.
  }

  const QJsonObject bufJson = BufferCoreService::getBuffer(p_bufferId);
  if (bufJson.isEmpty()) {
    // Closed underneath us (a nested event during the caller's drain). Treat
    // as a failure: the disk may still hold stale bytes and we can no longer
    // do anything about it.
    return fail(tr("A note was closed while the folder was being prepared."));
  }
  const QString notebookId = bufJson.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
  if (bufJson.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool()) {
    // A protected snapshot must retain the generation across callbacks and the
    // full durability barrier. Core authenticates and publishes the body snapshot.
    const auto buffer = protectedHandle(p_bufferId);
    VxCoreError error;
    auto lease = acquireProtectedLease(buffer, true, &error);
    if (!lease) {
      return fail(QString::fromUtf8(vxcore_error_message(error)));
    }
    if (m_saveQueue->isProtectedBusy(p_bufferId)) {
      return fail(tr("An open note is still being saved. Try again in a moment."));
    }
    if (buffer.isReadOnly()) {
      return fail(QString::fromUtf8(vxcore_error_message(VXCORE_ERR_READ_ONLY)));
    }
    BufferEvent event;
    event.bufferId = p_bufferId;
    if (m_hookMgr->doAction(HookNames::FileBeforeSave, event)) {
      return fail(tr("Saving an open note was cancelled."));
    }
    const auto writer = m_activeWriters.constFind(p_bufferId);
    const bool hasWriter = writer != m_activeWriters.constEnd() && bool(writer->callback);
    const QString content = hasWriter ? writer->callback() : QString();
    const auto revision = currentRevision(p_bufferId);
    QByteArray snapshot;
    {
      NotebookIoGate::ScopedTryLock gate(*m_ioGate, notebookId, p_gateTimeoutMs);
      if (!gate.isLocked()) {
        return fail(QString::fromUtf8(vxcore_error_message(VXCORE_ERR_SYNC_IN_PROGRESS)));
      }
      if (!lease->isCurrent() || m_saveQueue->isProtectedBusy(p_bufferId)) {
        return fail(tr("An open note is still being saved. Try again in a moment."));
      }
      if (hasWriter) {
        snapshot = encodeContent(p_bufferId, content);
      } else {
        snapshot = BufferCoreService::getContentRaw(p_bufferId, &error);
        if (error != VXCORE_OK)
          return fail(QString::fromUtf8(vxcore_error_message(error)));
      }
    }
    error = saveProtectedSnapshot(buffer, std::move(snapshot), revision, p_gateTimeoutMs);
    if (error != VXCORE_OK)
      return fail(QString::fromUtf8(vxcore_error_message(error)));
    if (currentRevision(p_bufferId) != revision || m_saveQueue->isProtectedBusy(p_bufferId)) {
      return fail(tr("The note changed while its snapshot was being saved."));
    }
    markRevisionSaved(p_bufferId, revision);
    m_hookMgr->doAction(HookNames::FileAfterSave, event);
    emit bufferModifiedChanged(p_bufferId);
    return true;
  }

  // Defense in depth: the caller drains first, but re-check here because
  // racing a worker on the mutex-less vxcore Buffer is a use-after-free class
  // of bug, not a mere ordering wart.
  if (m_saveQueue && m_saveQueue->isBusy(notebookId, p_bufferId)) {
    return fail(tr("An open note is still being saved. Try again in a moment."));
  }

  if (!pullActiveWriterContent(p_bufferId)) {
    return fail(tr("Could not read the latest content of an open note."));
  }

  if (!BufferCoreService::isModified(p_bufferId)) {
    return true; // Already durable.
  }

  if (isBufferReadOnly(p_bufferId)) {
    emit saveRejectedReadOnly(p_bufferId);
    return fail(tr("An open note has unsaved changes but its notebook is read-only."));
  }

  BufferEvent event;
  event.bufferId = p_bufferId;
  // Fired OUTSIDE the gate on purpose: NotebookIoGate is a plain (non-recursive)
  // mutex, so a hook that touched this notebook while we held it would
  // self-deadlock. The consequence is that the hook is callback-capable and may
  // enqueue work, which the re-check below catches.
  if (m_hookMgr && m_hookMgr->doAction(HookNames::FileBeforeSave, event)) {
    return fail(tr("Saving an open note was cancelled."));
  }

  // Capture the revision that this write will make durable BEFORE writing.
  // Re-sampling it afterwards would let an edit made by an after-save hook be
  // recorded as saved, clearing the dirty flag for content that is only in
  // memory — exactly the stale-content publish this barrier exists to prevent.
  const quint64 savedRevision = currentRevision(p_bufferId);

  {
    // Bounded: never freeze the GUI on a long sync stage.
    NotebookIoGate::ScopedTryLock lock(*m_ioGate, notebookId, p_gateTimeoutMs);
    if (!lock.isLocked()) {
      return fail(tr("The notebook is busy syncing. Try again in a moment."));
    }
    // LAST re-check, under the gate: everything above (the writer callback, the
    // modified/content-synced signals, the before-save hook) can synchronously
    // re-enter and enqueue an async save. Such a worker would block on this
    // gate and then overwrite our write with ITS older snapshot the moment we
    // release. Refuse instead of publishing what it would leave behind.
    if (m_saveQueue && m_saveQueue->isBusy(notebookId, p_bufferId)) {
      return fail(tr("An open note is still being saved. Try again in a moment."));
    }
    if (!BufferCoreService::saveBuffer(p_bufferId)) {
      return fail(tr("Could not write an open note to disk."));
    }
  }

  if (m_hookMgr) {
    m_hookMgr->doAction(HookNames::FileAfterSave, event);
  }
  markRevisionSaved(p_bufferId, savedRevision);
  emit bufferModifiedChanged(p_bufferId);
  return true;
}

bool BufferService::captureActiveWriterContent(const QString &p_bufferId,
                                               QString *p_outText) const {
  if (!p_outText || QThread::currentThread() != thread())
    return false;
  ContentFetchCallback callback;
  const auto writer = m_activeWriters.constFind(p_bufferId);
  if (writer != m_activeWriters.constEnd())
    callback = writer->callback;
  if (!callback && m_noteConversions) {
    const auto suspended = m_noteConversions->entries.constFind(p_bufferId);
    if (suspended != m_noteConversions->entries.constEnd() && suspended->hadWriter) {
      callback = suspended->writer.callback;
    }
  }
  if (!callback)
    return false;
  *p_outText = callback();
  return true;
}

bool BufferService::beginNoteConversion(const QString &p_bufferId, QByteArray *p_outBody) {
  if (!p_outBody || QThread::currentThread() != thread() || p_bufferId.isEmpty() ||
      isBufferReadOnly(p_bufferId) || (m_bufferFlags.value(p_bufferId) & Encrypted) != 0 ||
      m_virtualBufferIds.contains(p_bufferId) || isSaveQueueBusy(p_bufferId)) {
    return false;
  }
  if (m_noteConversions && m_noteConversions->entries.contains(p_bufferId))
    return false;
  if (BufferCoreService::getBuffer(p_bufferId).isEmpty())
    return false;
  if (!m_noteConversions)
    m_noteConversions.reset(new NoteConversions);
  NoteConversions::Entry entry;
  const auto writer = m_activeWriters.constFind(p_bufferId);
  if (writer != m_activeWriters.constEnd()) {
    entry.writer = writer.value();
    entry.hadWriter = true;
  }
  entry.wasDirty = m_dirtyBuffers.contains(p_bufferId);
  m_noteConversions->entries.insert(p_bufferId, std::move(entry));
  m_activeWriters.remove(p_bufferId);
  m_dirtyBuffers.remove(p_bufferId);
  m_bufferFlags[p_bufferId] |= Converting;
  if (m_dirtyBuffers.isEmpty())
    m_autoSaveTimer->stop();
  try {
    QString text;
    const bool edited = currentRevision(p_bufferId) != lastSavedRevision(p_bufferId) ||
                        BufferCoreService::isModified(p_bufferId);
    if (edited && captureActiveWriterContent(p_bufferId, &text)) {
      *p_outBody = encodeContent(p_bufferId, text);
    } else {
      VxCoreError error;
      *p_outBody = BufferCoreService::getContentRaw(p_bufferId, &error);
      if (error != VXCORE_OK) {
        endNoteConversion(p_bufferId, false);
        return false;
      }
    }
    if (isSaveQueueBusy(p_bufferId)) {
      endNoteConversion(p_bufferId, false);
      return false;
    }
    return true;
  } catch (...) {
    endNoteConversion(p_bufferId, false);
    throw;
  }
}

void BufferService::endNoteConversion(const QString &p_bufferId, bool p_committed) {
  if (!m_noteConversions)
    return;
  const auto found = m_noteConversions->entries.find(p_bufferId);
  if (found == m_noteConversions->entries.end())
    return;
  auto entry = std::move(found.value());
  m_noteConversions->entries.erase(found);
  auto flags = m_bufferFlags.find(p_bufferId);
  if (flags != m_bufferFlags.end()) {
    flags.value() &= static_cast<quint8>(~Converting);
    if (!p_committed) {
      if (entry.hadWriter)
        m_activeWriters.insert(p_bufferId, std::move(entry.writer));
      if (entry.wasDirty || currentRevision(p_bufferId) > lastSavedRevision(p_bufferId)) {
        m_dirtyBuffers.insert(p_bufferId);
      }
      if (!m_dirtyBuffers.isEmpty() && !m_autoSaveTimer->isActive())
        m_autoSaveTimer->start();
    }
  }
  if (m_noteConversions->entries.isEmpty())
    m_noteConversions.reset();
}

void BufferService::registerActiveWriter(const QString &p_bufferId, quintptr p_writerKey,
                                         ContentFetchCallback p_callback) {
  if (p_bufferId.isEmpty() || !p_callback) {
    return;
  }
  const bool encrypted = (m_bufferFlags.value(p_bufferId) & Encrypted) != 0;
  if (encrypted && m_protectedLocking)
    return;
  m_activeWriters[p_bufferId] = ActiveWriter{p_writerKey, std::move(p_callback), encrypted};
}

void BufferService::unregisterActiveWriter(const QString &p_bufferId, quintptr p_writerKey) {
  if (p_bufferId.isEmpty()) {
    return;
  }
  // Only unregister if this writer is the current active writer.
  auto it = m_activeWriters.find(p_bufferId);
  if (it != m_activeWriters.end() && it->key == p_writerKey) {
    m_activeWriters.erase(it);
  }
}

void BufferService::onAutoSaveTimerTick() {
  QElapsedTimer timer;
  timer.start();

  if (m_dirtyBuffers.isEmpty()) {
    m_autoSaveTimer->stop();
    return;
  }

  // Copy the set to iterate safely (executeSyncForBuffer may modify it indirectly).
  const auto dirtyBuffersCopy = m_dirtyBuffers;
  m_dirtyBuffers.clear();

  for (const auto &bufferId : dirtyBuffersCopy) {
    executeSyncForBuffer(bufferId);
  }

  if (m_dirtyBuffers.isEmpty()) {
    m_autoSaveTimer->stop();
  }

  qint64 elapsed = timer.elapsed();
  qCDebug(perfSave) << "[perf.save] tick_ms=" << elapsed;
}

bool BufferService::executeSyncForBuffer(const QString &p_bufferId) {
  QElapsedTimer timer;
  timer.start();

  // Virtual buffers have no file content to sync.
  if (m_virtualBufferIds.contains(p_bufferId)) {
    return true;
  }

  auto it = m_activeWriters.find(p_bufferId);
  if (it == m_activeWriters.end() || !it->callback) {
    // No active writer — content already synced from previous focus-loss.
    return true;
  }
  if (it->encrypted) {
    const QString content = it->callback();
    executeProtectedSync(protectedHandle(p_bufferId), content, currentRevision(p_bufferId));
    return false;
  }

  // Skip auto-save disk write for externally changed/missing files.
  // (Inline check before snapshot — cheap.)
  BufferState state = BufferCoreService::getState(p_bufferId);
  if (state == BufferState::FileChanged || state == BufferState::FileMissing) {
    return true;
  }

  // Fetch latest editor content as a QString snapshot — UI thread only.
  QString content = it->callback();

  // Capture the revision tied to this snapshot (T6). markRevisionSaved on
  // completion advances lastSavedRevision and clears dirty iff it catches up
  // to current.
  const quint64 capturedRev = currentRevision(p_bufferId);

  switch (m_autoSavePolicy) {
  case AutoSavePolicy::None: {
    // Sync to in-memory vxcore buffer only — no disk write, fast.
    if (BufferCoreService::setContentRaw(p_bufferId, encodeContent(p_bufferId, content))) {
      emit bufferContentSynced(p_bufferId);
      emit bufferModifiedChanged(p_bufferId);
    } else {
      qWarning() << "BufferService: failed to set content for buffer" << p_bufferId;
    }
    break;
  }

  case AutoSavePolicy::AutoSave: {
    // T7: dispatch off UI thread via BufferSaveQueue.
    // Resolve notebookId for gate keying.
    QJsonObject bufJson = BufferCoreService::getBuffer(p_bufferId);
    QString notebookId = bufJson.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();

    // Emit content-synced eagerly: the snapshot is committed to the queue and
    // the editor view can drop any "syncing" hint. The actual disk write
    // completes asynchronously via onSaveFinished.
    emit bufferContentSynced(p_bufferId);
    // Pass the raw override (empty when none) so the worker's UTF-8 fast path
    // is used for buffers without an encoding override.
    m_saveQueue->enqueue(notebookId, p_bufferId, content, capturedRev,
                         m_bufferEncodings.value(p_bufferId));
    break;
  }

  case AutoSavePolicy::BackupFile: {
    // Backup file does NOT contend with libgit2 (writes to .vswp, not the working tree)
    // and so remains inline. Still cheap (in-memory + sibling-file write).
    if (!BufferCoreService::setContentRaw(p_bufferId, encodeContent(p_bufferId, content))) {
      qWarning() << "BufferService: failed to set content for buffer" << p_bufferId;
      break;
    }
    emit bufferContentSynced(p_bufferId);
    emit bufferModifiedChanged(p_bufferId);

    bool backupOk = BufferCoreService::writeBackup(p_bufferId);
    if (backupOk) {
      qDebug() << "BufferService: backup written for buffer" << p_bufferId << "at"
               << BufferCoreService::getBackupPath(p_bufferId);
      m_saveFailureCounts.remove(p_bufferId);
      emit bufferAutoSaved(p_bufferId);
      emit bufferModifiedChanged(p_bufferId);
    } else {
      int failCount = m_saveFailureCounts.value(p_bufferId, 0) + 1;
      m_saveFailureCounts[p_bufferId] = failCount;
      qWarning() << "BufferService: BackupFile write failed for buffer" << p_bufferId << "(attempt"
                 << failCount << ")";
      emit bufferAutoSaveFailed(p_bufferId);
      if (failCount >= c_maxSaveFailures) {
        qWarning() << "BufferService: BackupFile aborted for buffer" << p_bufferId << "after"
                   << c_maxSaveFailures << "failures";
        emit bufferAutoSaveAborted(p_bufferId);
      } else {
        m_dirtyBuffers.insert(p_bufferId);
      }
    }
    break;
  }
  }

  qint64 elapsed = timer.elapsed();
  qCDebug(perfSave) << "[perf.save] execute_ms=" << elapsed;
  return true;
}

void BufferService::onSaveFinished(const QString &p_bufferId, quint64 p_revision, bool p_ok,
                                   const QString &p_errorMsg) {
  if (p_ok) {
    // T6: advances lastSavedRevision and clears dirty iff lastSaved == current.
    markRevisionSaved(p_bufferId, p_revision);
    m_saveFailureCounts.remove(p_bufferId);
    emit bufferAutoSaved(p_bufferId);
    emit bufferModifiedChanged(p_bufferId);
  } else {
    int failCount = m_saveFailureCounts.value(p_bufferId, 0) + 1;
    m_saveFailureCounts[p_bufferId] = failCount;
    qWarning() << "BufferService: AutoSave failed for buffer" << p_bufferId << "(attempt"
               << failCount << "):" << p_errorMsg;
    emit saveError(p_bufferId, p_errorMsg);
    emit bufferAutoSaveFailed(p_bufferId);
    if (failCount >= c_maxSaveFailures) {
      qWarning() << "BufferService: AutoSave aborted for buffer" << p_bufferId << "after"
                 << c_maxSaveFailures << "failures";
      emit bufferAutoSaveAborted(p_bufferId);
    } else {
      // Re-mark dirty so the next timer tick retries.
      m_dirtyBuffers.insert(p_bufferId);
      if (!m_autoSaveTimer->isActive()) {
        m_autoSaveTimer->start();
      }
    }
  }
}

void BufferService::executeProtectedSync(const Buffer2 &p_buffer, const QString &p_content,
                                         quint64 p_revision) {
  if (!p_buffer.isValid()) {
    return;
  }
  const QString &bufferId = p_buffer.m_bufferId;
  m_dirtyBuffers.insert(bufferId);
  if (m_saveFailureCounts.value(bufferId, 0) >= c_maxSaveFailures) {
    return;
  }
  if (p_buffer.isReadOnly()) {
    emit saveRejectedReadOnly(bufferId);
    return;
  }
  if (m_saveQueue->isProtectedBusy(bufferId) &&
      p_revision <= p_buffer.m_protectedState->lastQueuedRevision) {
    return;
  }
  VxCoreError error;
  if (m_autoSavePolicy == AutoSavePolicy::None) {
    error = withProtectedBuffer(p_buffer, true, [&]() {
      if (m_saveQueue->isProtectedBusy(bufferId)) {
        return VXCORE_ERR_INVALID_STATE;
      }
      VxCoreError status;
      BufferCoreService::setContentRaw(bufferId, encodeContent(bufferId, p_content), &status);
      return status;
    });
    if (error == VXCORE_OK) {
      if (currentRevision(bufferId) == p_revision) {
        m_dirtyBuffers.remove(bufferId);
      }
      emit bufferContentSynced(bufferId);
      emit bufferModifiedChanged(bufferId);
      return;
    }
  } else {
    auto lease = acquireProtectedLease(p_buffer, true, &error);
    if (lease &&
        m_saveQueue->enqueueProtected(*this, p_buffer.nodeId().notebookId, lease, p_content,
                                      p_revision, m_bufferEncodings.value(bufferId),
                                      m_autoSavePolicy == AutoSavePolicy::BackupFile)) {
      p_buffer.m_protectedState->lastQueuedRevision = p_revision;
      emit bufferContentSynced(bufferId);
      return;
    }
    if (error == VXCORE_OK) {
      error = VXCORE_ERR_INVALID_STATE;
    }
  }
  onProtectedSaveFinished(bufferId, p_buffer.m_protectedState->generation, p_revision,
                          m_autoSavePolicy == AutoSavePolicy::BackupFile, int(error));
}

void BufferService::onProtectedSaveFinished(const QString &p_bufferId, quint64 p_generation,
                                            quint64 p_revision, bool p_backup, int p_error) {
  const auto buffer = protectedHandle(p_bufferId);
  if (!buffer.isValid() || buffer.m_protectedState->generation != p_generation) {
    emit protectedSaveFinished(p_bufferId, p_generation, p_revision, p_backup,
                               int(VXCORE_ERR_INVALID_STATE));
    return;
  }
  if (p_error == VXCORE_OK) {
    if (p_backup) {
      if (currentRevision(p_bufferId) == p_revision) {
        m_dirtyBuffers.remove(p_bufferId);
      }
    } else {
      markRevisionSaved(p_bufferId, p_revision);
    }
    m_saveFailureCounts.remove(p_bufferId);
    emit bufferAutoSaved(p_bufferId);
    emit bufferModifiedChanged(p_bufferId);
  } else {
    // Failure never clears dirty editor state, including when retries stop.
    m_dirtyBuffers.insert(p_bufferId);
    const int failures = m_saveFailureCounts.value(p_bufferId, 0) + 1;
    m_saveFailureCounts.insert(p_bufferId, failures);
    emit saveError(p_bufferId, QString::fromUtf8(vxcore_error_message(VxCoreError(p_error))));
    emit bufferAutoSaveFailed(p_bufferId);
    if (failures >= c_maxSaveFailures) {
      emit bufferAutoSaveAborted(p_bufferId);
    } else if (!m_autoSaveTimer->isActive()) {
      m_autoSaveTimer->start();
    }
  }
  emit protectedSaveFinished(p_bufferId, p_generation, p_revision, p_backup, p_error);
}
