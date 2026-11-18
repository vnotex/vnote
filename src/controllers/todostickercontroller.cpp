#include "todostickercontroller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QResource>
#include <QScopeGuard>
#include <QSet>
#include <QTextDocument>
#include <QTimer>
#include <utility>

#include <core/configmgr2.h>
#include <core/fileopensettings.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/noteoperationservice.h>
#include <core/services/syncworkqueuemanager.h>
#include <core/sessionconfig.h>
#include <models/todomodels.h>
#include <vxcore/notebook_json_keys.h>

#include "todoviewwindowcontroller.h"

using namespace vnotex;

namespace {
#ifdef Q_OS_WIN
constexpr auto c_pathSensitivity = Qt::CaseInsensitive;
#else
constexpr auto c_pathSensitivity = Qt::CaseSensitive;
#endif

bool samePath(const QString &p_left, const QString &p_right) {
  return QDir::cleanPath(p_left).compare(QDir::cleanPath(p_right), c_pathSensitivity) == 0;
}

bool containsPath(const QString &p_parent, const QString &p_path, bool p_folder) {
  return samePath(p_parent, p_path) ||
         (p_folder && p_path.startsWith(p_parent + QLatin1Char('/'), c_pathSensitivity));
}

bool linkedPath(const QString &p_path) {
  for (QFileInfo part(p_path);; part = QFileInfo(part.absolutePath())) {
    if (part.isSymbolicLink() || part.isJunction()) {
      return true;
    }
    if (part.isRoot()) {
      return false;
    }
  }
}

// All resolution is on the GUI thread. Hooks may initialize another dashboard
// synchronously while openNotebook/createFileWithContent is on the stack.
QSet<const ConfigMgr2 *> s_resolvingDefaults;
QSet<QString> s_ensuringFiles;
} // namespace

TodoStickerController::TodoStickerController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_taskModel(new TodoTaskModel(p_services, this)) {
  connect(m_taskModel, &TodoTaskModel::checkedChangeRequested, this,
          &TodoStickerController::setChecked);
}

TodoStickerController::~TodoStickerController() {
  ++m_generation;
  cancelEdit();
  if (m_hooks) {
    for (const int id : m_hookIds) {
      m_hooks->removeAction(id);
    }
  }
}

void TodoStickerController::setDefaultNotebookPathForTest(const QString &p_path) {
  if (!m_initialized) {
    m_defaultNotebookPathForTest = p_path;
  }
}

void TodoStickerController::initialize() {
  if (m_initialized) {
    refresh();
    return;
  }
  m_initialized = true;
  subscribe();
  refresh();
}

void TodoStickerController::subscribe() {
  if (auto *config = m_services.get<ConfigMgr2>()) {
    m_currentNotebook = config->getSessionConfig().getCurrentNotebook();
    connect(config, &ConfigMgr2::sessionConfigChanged, this, [this, config]() {
      const auto &session = config->getSessionConfig();
      if (session.getTodoStickerFilePath() != m_filePath) {
        changeBinding(session.getTodoStickerFilePath());
        scheduleRefresh();
      } else if (m_filePath.isEmpty() && session.getCurrentNotebook() != m_currentNotebook) {
        scheduleRefresh();
      }
      m_currentNotebook = session.getCurrentNotebook();
    });
  }
  if (auto *buffers = m_services.get<BufferService>()) {
    // BufferService's QObject base is deliberately private.
    connect(buffers->asQObject(), SIGNAL(bufferPublicationChanged(QString)), this,
            SLOT(handleBufferChanged(QString)));
    connect(buffers->asQObject(), SIGNAL(bufferContentSynced(QString)), this,
            SLOT(handleBufferChanged(QString)));
    connect(buffers->asQObject(), SIGNAL(bufferModifiedChanged(QString)), this,
            SLOT(handleBufferChanged(QString)));
    connect(buffers->asQObject(), SIGNAL(bufferAutoSaved(QString)), this,
            SLOT(handleBufferChanged(QString)));
    connect(buffers->asQObject(), SIGNAL(contentReplacementStateChanged(QString, bool, bool, bool)),
            this, SLOT(handleBufferChanged(QString)));
  }
  m_hooks = m_services.get<HookManager>();
  if (!m_hooks) {
    return;
  }
  const QPointer<TodoStickerController> self(this);
  m_hookIds.append(m_hooks->addAction<NotebookOpenEvent>(
      HookNames::NotebookAfterOpen, [self](HookContext &, const NotebookOpenEvent &) {
        if (self) {
          self->scheduleRefresh();
        }
      }));
  m_hookIds.append(m_hooks->addAction<NotebookCloseEvent>(
      HookNames::NotebookAfterClose, [self](HookContext &, const NotebookCloseEvent &p_event) {
        if (self && self->m_node.notebookId == p_event.notebookId) {
          ++self->m_generation;
          self->cancelEdit();
          self->unavailable(tr("The TODO notebook is closed. Reopen it or choose another file."));
        }
      }));
  m_hookIds.append(m_hooks->addAction<NodeRenameEvent>(
      HookNames::NodeAfterRename, [self](HookContext &, const NodeRenameEvent &p_event) {
        if (self) {
          const int slash = p_event.relativePath.lastIndexOf(QLatin1Char('/'));
          const auto newPath = p_event.relativePath.left(slash + 1) + p_event.newName;
          self->retarget(p_event.notebookId, p_event.relativePath, p_event.notebookId, newPath,
                         p_event.isFolder);
        }
      }));
  m_hookIds.append(m_hooks->addAction<NodeMoveEvent>(
      HookNames::NodeAfterMove, [self](HookContext &, const NodeMoveEvent &p_event) {
        if (self) {
          self->retarget(p_event.notebookId, p_event.oldRelativePath, p_event.notebookId,
                         p_event.newRelativePath, p_event.isFolder);
        }
      }));
  m_hookIds.append(m_hooks->addAction<NodeTransferEvent>(
      HookNames::NodeAfterTransfer, [self](HookContext &, const NodeTransferEvent &p_event) {
        if (self && !p_event.sourceRemains && p_event.requestedOperation == QLatin1String("move")) {
          self->retarget(p_event.sourceNotebookId, p_event.sourceRelativePath,
                         p_event.destinationNotebookId, p_event.destinationRelativePath,
                         p_event.isFolder);
        }
      }));
  m_hookIds.append(m_hooks->addAction<NodeOperationEvent>(
      HookNames::NodeAfterDelete, [self](HookContext &, const NodeOperationEvent &p_event) {
        if (self && self->m_node.notebookId == p_event.notebookId &&
            containsPath(p_event.relativePath, self->m_node.relativePath, p_event.isFolder)) {
          ++self->m_generation;
          self->cancelEdit();
          self->unavailable(tr("The TODO file was deleted. Refresh to recreate it."));
        }
      }));
  m_hookIds.append(m_hooks->addAction<NodeOperationEvent>(
      HookNames::NodeAfterCreate, [self](HookContext &, const NodeOperationEvent &p_event) {
        if (self && (self->m_filePath.isEmpty() || self->m_node.notebookId == p_event.notebookId)) {
          self->scheduleRefresh();
        }
      }));
  m_hookIds.append(m_hooks->addAction<FileExternalChangeEvent>(
      HookNames::FileExternalChange, [self](HookContext &, const FileExternalChangeEvent &p_event) {
        if (self) {
          self->handleBufferChanged(p_event.bufferId);
        }
      }));
  m_hookIds.append(m_hooks->addAction<BufferEvent>(
      HookNames::FileAfterReload, [self](HookContext &, const BufferEvent &p_event) {
        if (self) {
          self->handleBufferChanged(p_event.bufferId);
        }
      }));
}

void TodoStickerController::scheduleRefresh() {
  // Reads open and close private buffers themselves: their publications must
  // never turn one unavailable read into an event-loop polling cycle.
  if (!m_initialized || m_editing || m_refreshScheduled) {
    return;
  }
  if (m_refreshing) {
    m_refreshAgain = true;
    return;
  }
  m_refreshScheduled = true;
  QTimer::singleShot(0, this, [this]() {
    m_refreshScheduled = false;
    refresh();
  });
}

void TodoStickerController::handleBufferChanged(const QString &p_bufferId) {
  if (m_refreshing || m_editing || m_filePath.isEmpty()) {
    return;
  }
  auto *buffers = m_services.get<BufferService>();
  const auto buffer = buffers->getBufferHandle(p_bufferId);
  if (buffer.isValid() &&
      (buffer.nodeId() == m_node || samePath(buffer.resolvedPath(), m_filePath))) {
    scheduleRefresh();
  }
}

void TodoStickerController::cancelEdit() {
  const int token = m_editToken;
  m_editToken = 0;
  m_editing = false;
  m_pendingListKey.clear();
  m_pendingTaskKey.clear();
  if (token) {
    if (auto *buffers = m_services.get<BufferService>()) {
      buffers->cancelSearchReplacement(token);
    }
  }
}

void TodoStickerController::changeBinding(const QString &p_path) {
  ++m_generation;
  cancelEdit();
  m_filePath = p_path;
  m_node = {};
  m_notebookRoot.clear();
  if (m_view != TodoViewKind::All || !m_dueDate.isValid()) {
    m_view = TodoViewKind::List;
  }
  m_selectedListKey.clear();
  m_document = {};
  m_ready = false;
  m_readOnly = true;
  ++m_revision;
  publishModel();
}

void TodoStickerController::publishModel() {
  TodoProjection projection;
  projection.m_view = m_view;
  projection.m_listKey = m_selectedListKey;
  projection.m_completedExpanded = true;
  projection.m_today = m_today;
  if (m_view == TodoViewKind::All) {
    projection.m_dueDate = m_dueDate;
  }
  m_taskModel->setSnapshot(m_document, projection,
                           m_ready && !m_readOnly && !m_editing && m_document.isValid(),
                           m_revision);
}

void TodoStickerController::reportError(const QString &p_error) {
  m_error = p_error;
  const QPointer<TodoStickerController> self(this);
  emit stateChanged();
  if (self && !p_error.isEmpty()) {
    emit errorOccurred(p_error);
  }
}

void TodoStickerController::unavailable(const QString &p_error) {
  m_ready = false;
  m_readOnly = true;
  m_refreshing = false;
  m_document = {};
  ++m_revision;
  publishModel();
  reportError(p_error);
}

bool TodoStickerController::resolveFile(const QString &p_path, NodeIdentifier *p_node,
                                        QString *p_canonical, QString *p_error,
                                        bool p_allowMissing) const {
  const QFileInfo file(p_path);
  if (!QDir::isAbsolutePath(p_path) ||
      file.suffix().compare(QLatin1String("todo"), Qt::CaseInsensitive)) {
    *p_error = tr("Choose an absolute path to a .todo file.");
    return false;
  }
  if (linkedPath(p_path)) {
    *p_error = tr("Linked TODO files or folders are unavailable. Choose a regular file.");
    return false;
  }
  if (file.exists()) {
    if (!file.isFile() || !file.isReadable()) {
      *p_error = tr("The TODO file is not a readable regular file: %1").arg(p_path);
      return false;
    }
    *p_canonical = file.canonicalFilePath();
  } else {
    if (!p_allowMissing) {
      *p_error = tr("The TODO file is missing or unreadable: %1").arg(p_path);
      return false;
    }
    const QFileInfo parent(file.absolutePath());
    if (!parent.isDir() || parent.canonicalFilePath().isEmpty()) {
      *p_error = tr("The TODO parent folder is missing or unavailable: %1").arg(parent.filePath());
      return false;
    }
    *p_canonical = QDir(parent.canonicalFilePath()).filePath(file.fileName());
  }
  if (p_canonical->isEmpty()) {
    *p_error = tr("The TODO file path cannot be resolved: %1").arg(p_path);
    return false;
  }
  // The native resolver compares root components literally, including a trailing
  // slash. Resolve canonical Qt paths instead, keeping nested notebooks distinct.
  const auto contained = [](const QString &p_root, const QString &p_absolute) {
    const auto relative = QDir(p_root).relativeFilePath(p_absolute);
    return !QDir::isAbsolutePath(relative) && relative != QLatin1String("..") &&
           !relative.startsWith(QLatin1String("../"));
  };
  *p_node = {{}, *p_canonical};
  QString enclosingRoot;
  auto *notebooks = m_services.get<NotebookCoreService>();
  for (const auto &entry : notebooks->listNotebooks()) {
    const auto notebook = entry.toObject();
    const auto root =
        QFileInfo(notebook.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString())
            .canonicalFilePath();
    const auto id = notebook.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    if (!root.isEmpty() && !id.isEmpty() && root.size() > enclosingRoot.size() &&
        contained(root, *p_canonical)) {
      enclosingRoot = root;
      *p_node = {id, QDir(root).relativeFilePath(*p_canonical)};
    }
  }
  if (!m_notebookRoot.isEmpty() && samePath(p_path, m_filePath)) {
    const auto knownRoot = QFileInfo(m_notebookRoot).canonicalFilePath();
    if (!knownRoot.isEmpty() && knownRoot.size() > enclosingRoot.size() &&
        contained(knownRoot, *p_canonical)) {
      *p_error = tr("The TODO notebook is closed. Reopen it or choose another file.");
      return false;
    }
  }
  return true;
}

bool TodoStickerController::ensureBackingFile(const QString &p_path, NodeIdentifier *p_node,
                                              QString *p_canonical, QString *p_error) {
  if (!resolveFile(p_path, p_node, p_canonical, p_error, true)) {
    return false;
  }
  const auto node = *p_node;
  const auto path = *p_canonical;
  auto *notebooks = m_services.get<NotebookCoreService>();
  // Remember ownership even if creation or the subsequent snapshot is denied.
  // A later close must not turn this known notebook path into an external file.
  m_node = node;
  m_notebookRoot =
      node.notebookId.isEmpty() ? QString() : notebooks->buildAbsolutePath(node.notebookId, {});
  const auto key = c_pathSensitivity == Qt::CaseInsensitive ? path.toCaseFolded() : path;
  if (s_ensuringFiles.contains(key)) {
    *p_error = tr("The TODO file is being initialized by another dashboard.");
    return false;
  }
  s_ensuringFiles.insert(key);
  const auto ensuring = qScopeGuard([key]() { s_ensuringFiles.remove(key); });
  const QPointer<TodoStickerController> self(this);
  const auto generation = m_generation;
  auto *buffers = m_services.get<BufferService>();
  const QFileInfo file(path);
  QString parent =
      node.notebookId.isEmpty() ? file.absolutePath() : QFileInfo(node.relativePath).path();
  if (parent == QLatin1String(".")) {
    parent.clear();
  }
  if (file.exists()) {
    if (node.notebookId.isEmpty() || notebooks->getNotebookConfig(node.notebookId)
                                             .value(QLatin1String(vxcore::kJsonKeyType))
                                             .toString() != QLatin1String("bundled")) {
      // Raw reads reconcile their index themselves; indexNode is unsupported.
      return true;
    }
    VxCoreError listingError = VXCORE_OK;
    const auto children = notebooks->listFolderChildren(node.notebookId, parent, &listingError);
    if (listingError != VXCORE_OK) {
      *p_error = tr("The TODO notebook folder cannot be read.");
      return false;
    }
    for (const auto &entry : children.value(QLatin1String(vxcore::kJsonKeyFiles)).toArray()) {
      if (entry.toObject()
              .value(QLatin1String(vxcore::kJsonKeyName))
              .toString()
              .compare(file.fileName(), c_pathSensitivity) == 0) {
        return true;
      }
    }
    for (const auto &entry : children.value(QLatin1String(vxcore::kJsonKeyFolders)).toArray()) {
      if (entry.toObject()
              .value(QLatin1String(vxcore::kJsonKeyName))
              .toString()
              .compare(file.fileName(), c_pathSensitivity) == 0) {
        *p_error = tr("A notebook folder already occupies the TODO file path.");
        return false;
      }
    }
  }
  if ((!node.notebookId.isEmpty() && notebooks->isNotebookReadOnly(node.notebookId)) ||
      !QFileInfo(file.absolutePath()).isWritable()) {
    *p_error = tr("The TODO location is read-only; the file cannot be created or indexed.");
    return false;
  }
  SyncWorkQueueManager::MaintenanceLease maintenance;
  auto *gate = m_services.get<NotebookIoGate>();
  if (!node.notebookId.isEmpty()) {
    auto *queues = m_services.get<SyncWorkQueueManager>();
    if (!queues || !gate) {
      *p_error = tr("The notebook maintenance service is unavailable.");
      return false;
    }
    maintenance = queues->tryAcquireMaintenance({node.notebookId});
    if (!maintenance) {
      *p_error = tr("The notebook is busy syncing. Try again after sync finishes.");
      return false;
    }
  }
  if (file.exists()) {
    // Index existing bundled bytes without ever substituting the initial body.
    NotebookIoGate::ScopedTryLock lock(*gate, node.notebookId, 50);
    if (!lock.isLocked() || linkedPath(path) || !QFileInfo(path).isFile() ||
        notebooks->isNotebookReadOnly(node.notebookId) ||
        !notebooks->indexNode(node.notebookId, node.relativePath)) {
      *p_error = tr("The existing TODO file could not be indexed safely.");
      return false;
    }
    return true;
  }
  // No notebook child/file-info query is allowed before recreating a missing
  // raw body: raw reconciliation would remove the original UUID and metadata.
  Buffer2 live;
  for (const auto &entry : buffers->listBuffers()) {
    const auto info = entry.toObject();
    const auto id = info.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    const auto notebookId = info.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
    const auto relativePath = info.value(QStringLiteral("filePath")).toString();
    const auto absolute = notebookId.isEmpty()
                              ? relativePath
                              : notebooks->buildAbsolutePath(notebookId, relativePath);
    if (!samePath(absolute, path)) {
      continue;
    }
    if (live.isValid() && live.id() != id) {
      *p_error = tr("The TODO file has competing open buffers. Close the extra views and retry.");
      return false;
    }
    live = buffers->getBufferHandle(id);
    if (!live.isValid() || live.nodeId().notebookId != node.notebookId ||
        !samePath(live.resolvedPath(), path) || !live.isFileBacked() || live.isMemoryBacked() ||
        live.isEncrypted() || live.isReadOnly() || !live.canSave()) {
      *p_error =
          tr("The open TODO buffer cannot safely recreate this file. Save it from its editor.");
      return false;
    }
  }
  if (live.isValid()) {
    const auto publication = buffers->snapshotPublication(live.id());
    if (!publication.live || publication.busy || buffers->isContentReplacementActive(live.id())) {
      *p_error =
          tr("The open TODO buffer is busy. Try again after the current operation finishes.");
      return false;
    }
    // Capture even legacy writers. Buffer2::save() then uses the service's
    // worker queue; never hold NotebookIoGate while waiting for that worker.
    const bool captured = buffers->pullActiveWriterContent(live.id());
    if (!self || generation != m_generation) {
      return false;
    }
    if (!captured) {
      *p_error = tr("The current TODO draft could not be captured. Save it from its editor.");
      return false;
    }
    const auto current = buffers->snapshotPublication(live.id());
    live = buffers->getBufferHandle(live.id());
    if (!current.live || current.busy || current.bufferEpoch != publication.bufferEpoch ||
        buffers->isContentReplacementActive(live.id()) || !live.isValid() ||
        live.nodeId().notebookId != node.notebookId || !samePath(live.resolvedPath(), path) ||
        !live.isFileBacked() || live.isMemoryBacked() || live.isEncrypted() || live.isReadOnly() ||
        !live.canSave() || linkedPath(path) ||
        !samePath(QFileInfo(file.absolutePath()).canonicalFilePath(), file.absolutePath()) ||
        (!node.notebookId.isEmpty() && notebooks->isNotebookReadOnly(node.notebookId))) {
      *p_error = tr("The open TODO buffer changed or became unavailable before it could be saved.");
      return false;
    }
    if (QFileInfo::exists(path)) {
      // Another actor restored the body during writer capture; leave its bytes alone.
      return resolveFile(path, p_node, p_canonical, p_error);
    }
    VxCoreError saveError = VXCORE_OK;
    const bool saved = live.save(&saveError);
    if (!self || generation != m_generation) {
      return false;
    }
    if (!saved) {
      *p_error = saveError == VXCORE_ERR_CANCELLED
                     ? tr("Saving the TODO draft was denied or cancelled. Save it from its editor.")
                     : tr("The TODO draft could not be saved to recreate its file (error %1). "
                          "The draft remains open in its editor.")
                           .arg(static_cast<int>(saveError));
      return false;
    }
  } else {
    const auto resourcePath = QStringLiteral(":/vnotex/data/extra/default-notebook/My TODOs.todo");
    const auto resourceBundle = QStringLiteral("app:vnote_extra.rcc");
    const bool mounted =
        !QFileInfo::exists(resourcePath) && QResource::registerResource(resourceBundle);
    const auto unmount = qScopeGuard([&]() {
      if (mounted) {
        QResource::unregisterResource(resourceBundle);
      }
    });
    QFile resource(resourcePath);
    if (!resource.open(QIODevice::ReadOnly)) {
      *p_error = tr("The bundled initial TODO document is unavailable.");
      return false;
    }
    const auto initial = resource.readAll();
    if (resource.error() != QFileDevice::NoError || initial.isEmpty()) {
      *p_error = tr("The bundled initial TODO document could not be read.");
      return false;
    }
    const auto created =
        notebooks->createFileWithContent(node.notebookId, parent, file.fileName(), initial, true);
    if (!self || generation != m_generation) {
      return false;
    }
    if (!created.saved || !created.errorCode.isEmpty()) {
      *p_error = created.errorMessage.isEmpty() ? tr("The TODO file could not be created.")
                                                : created.errorMessage;
      return false;
    }
    if (!samePath(created.relativePath, node.relativePath)) {
      *p_error =
          tr("The TODO destination changed during creation. No alternate file was selected.");
      return false;
    }
  }
  NodeIdentifier resolved;
  QString canonical;
  if (!resolveFile(path, &resolved, &canonical, p_error) || resolved != node ||
      !samePath(canonical, path)) {
    if (p_error->isEmpty()) {
      *p_error = tr("The TODO file changed while it was being recreated. Refresh and try again.");
    }
    return false;
  }
  *p_canonical = canonical;
  return true;
}

QString TodoStickerController::resolveDefault(QString *p_error) {
  auto *config = m_services.get<ConfigMgr2>();
  auto *notebooks = m_services.get<NotebookCoreService>();
  if (s_resolvingDefaults.contains(config)) {
    *p_error = tr("The default TODO file is being initialized by another dashboard.");
    return {};
  }
  s_resolvingDefaults.insert(config);
  const auto resolving = qScopeGuard([config]() { s_resolvingDefaults.remove(config); });
  const QPointer<TodoStickerController> self(this);
  const auto defaultPath = m_defaultNotebookPathForTest.isEmpty()
                               ? ConfigMgr2::getDefaultNotebookPath()
                               : m_defaultNotebookPathForTest;
  const auto canonicalDefault = QFileInfo(defaultPath).canonicalFilePath();
  const auto opened = notebooks->listNotebooks();
  QString notebookId;
  const auto current = config->getSessionConfig().getCurrentNotebook();
  for (const auto &entry : opened) {
    const auto notebook = entry.toObject();
    const auto id = notebook.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    const auto root = notebook.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
    if (!canonicalDefault.isEmpty() &&
        samePath(QFileInfo(root).canonicalFilePath(), canonicalDefault)) {
      notebookId = id;
      break;
    }
    if (notebookId.isEmpty() || id == current) {
      notebookId = id;
    }
  }
  if (notebookId.isEmpty()) {
    notebookId = notebooks->openNotebook(defaultPath);
    if (!self) {
      return {};
    }
    if (notebookId.isEmpty()) {
      *p_error = tr("No notebook is available. Open a notebook or choose a TODO file in Settings.");
      return {};
    }
  }
  // Hooks may have selected an explicit file while the notebook opened.
  if (!config->getSessionConfig().getTodoStickerFilePath().isEmpty()) {
    return config->getSessionConfig().getTodoStickerFilePath();
  }
  const auto root = notebooks->buildAbsolutePath(notebookId, {});
  const auto name = QStringLiteral("My TODOs.todo");
  auto path = QDir(root).filePath(name);
  const QFileInfo rootInfo(root);
  if (root.isEmpty() || !rootInfo.isDir() || linkedPath(path)) {
    *p_error = tr("The default TODO location is missing or contains a linked path.");
    return {};
  }
#ifdef Q_OS_WIN
  // Windows treats differently cased spellings as the same logical path.
  const auto entries = QDir(root).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System |
                                                QDir::NoDotAndDotDot);
  for (const auto &entry : entries) {
    if (entry.fileName().compare(name, Qt::CaseInsensitive) == 0) {
      path = entry.absoluteFilePath();
      break;
    }
  }
#endif
  return path;
}

void TodoStickerController::refresh() {
  if (!m_initialized || m_refreshing || m_editing) {
    return;
  }
  auto *config = m_services.get<ConfigMgr2>();
  auto *buffers = m_services.get<BufferService>();
  auto *notebooks = m_services.get<NotebookCoreService>();
  if (!config || !buffers || !notebooks) {
    unavailable(tr("The TODO services are unavailable."));
    return;
  }
  m_refreshing = true;
  const QPointer<TodoStickerController> self(this);
  const auto refreshing = qScopeGuard([self]() {
    if (self) {
      self->m_refreshing = false;
      if (self->m_refreshAgain) {
        self->m_refreshAgain = false;
        self->scheduleRefresh();
      }
    }
  });
  QString path = config->getSessionConfig().getTodoStickerFilePath();
  QString error;
  if (path.isEmpty()) {
    path = resolveDefault(&error);
    if (!self) {
      return;
    }
    const auto selected = config->getSessionConfig().getTodoStickerFilePath();
    if (!selected.isEmpty()) {
      path = selected;
    }
    if (path.isEmpty()) {
      unavailable(error);
      return;
    }
  }
  if (path != m_filePath) {
    changeBinding(path);
  }
  const auto generation = m_generation;
  NodeIdentifier node;
  QString canonical;
  const bool ensured = ensureBackingFile(path, &node, &canonical, &error);
  if (!self || generation != m_generation) {
    return;
  }
  if (!ensured) {
    unavailable(error);
    return;
  }
  QString source;
  bool readOnly = true;
  const bool read = buffers->readTextSnapshot(node, &source, &readOnly, &error);
  if (!self || generation != m_generation) {
    return;
  }
  if (!read) {
    unavailable(error.isEmpty() ? tr("The TODO file is temporarily unavailable. Try again after "
                                     "the current operation finishes.")
                                : error);
    return;
  }
  m_document = TodoDocument::parse(source);
  m_ready = true;
  m_readOnly = readOnly || !QFileInfo(canonical).isWritable();
  if (m_view == TodoViewKind::List && !m_document.findList(m_selectedListKey)) {
    m_selectedListKey = m_document.m_lists.isEmpty() ? QString() : m_document.m_lists.first().m_key;
  }
  ++m_revision;
  m_error.clear();
  if (!m_document.isValid()) {
    const auto &diagnostic = m_document.m_diagnostics.first();
    m_error = tr("Line %1: %2").arg(diagnostic.m_line).arg(diagnostic.m_message);
  }
  // Persist default selection only after a real, authoritative read succeeds.
  // Malformed/blank existing documents are still legitimate chosen files.
  if (config->getSessionConfig().getTodoStickerFilePath().isEmpty()) {
    m_filePath = canonical;
    config->getSessionConfig().setTodoStickerFilePath(canonical);
    if (!self || generation != m_generation) {
      return;
    }
  }
  m_refreshing = false;
  publishModel();
  emit stateChanged();
}

void TodoStickerController::setView(TodoViewKind p_view, const QString &p_listKey) {
  if (!m_ready || isBusy() ||
      (p_view != TodoViewKind::List && p_view != TodoViewKind::MyDay &&
       p_view != TodoViewKind::Important && p_view != TodoViewKind::Planned &&
       (p_view != TodoViewKind::All || !m_dueDate.isValid()))) {
    return;
  }
  const auto listKey = p_view == TodoViewKind::List ? p_listKey : QString();
  if ((p_view == TodoViewKind::List && !m_document.findList(listKey)) ||
      (p_view == m_view && listKey == m_selectedListKey)) {
    return;
  }
  m_view = p_view;
  m_selectedListKey = listKey;
  ++m_revision;
  publishModel();
  emit stateChanged();
}

void TodoStickerController::setToday(const QDate &p_today) {
  if (m_today == p_today) {
    return;
  }
  m_today = p_today;
  if (m_view == TodoViewKind::MyDay) {
    ++m_revision;
    publishModel();
    emit stateChanged();
  }
}

void TodoStickerController::setDueDate(const QDate &p_dueDate) {
  if (!p_dueDate.isValid() || (m_view == TodoViewKind::All && m_dueDate == p_dueDate)) {
    return;
  }
  m_dueDate = p_dueDate;
  m_view = TodoViewKind::All;
  m_selectedListKey.clear();
  ++m_revision;
  publishModel();
  emit stateChanged();
}

bool TodoStickerController::setFilePath(const QString &p_path) {
  if (p_path.isEmpty()) {
    return false; // Cancelling the picker never clears the global selection.
  }
  if (m_refreshing) {
    reportError(tr("Wait for the current TODO operation to finish."));
    return false;
  }
  auto *config = m_services.get<ConfigMgr2>();
  auto *buffers = m_services.get<BufferService>();
  auto *notebooks = m_services.get<NotebookCoreService>();
  if (!config || !buffers || !notebooks) {
    reportError(tr("The TODO services are unavailable."));
    return false;
  }
  const QPointer<TodoStickerController> self(this);
  NodeIdentifier node;
  QString canonical;
  QString error;
  if (!resolveFile(p_path, &node, &canonical, &error)) {
    reportError(error);
    return false;
  }
  const auto generation = m_generation;
  QString source;
  bool readOnly = true;
  m_refreshing = true;
  const auto refreshing = qScopeGuard([self]() {
    if (self) {
      self->m_refreshing = false;
      if (self->m_refreshAgain) {
        self->m_refreshAgain = false;
        self->scheduleRefresh();
      }
    }
  });
  const bool read = buffers->readTextSnapshot(node, &source, &readOnly, &error);
  if (!self) {
    return false;
  }
  m_refreshing = false;
  if (generation != m_generation) {
    return false;
  }
  if (!read) {
    reportError(error.isEmpty() ? tr("The selected TODO file cannot be read right now.") : error);
    return false;
  }
  if (!m_initialized) {
    m_initialized = true;
    subscribe();
  }
  changeBinding(canonical);
  m_node = node;
  m_notebookRoot =
      node.notebookId.isEmpty() ? QString() : notebooks->buildAbsolutePath(node.notebookId, {});
  m_document = TodoDocument::parse(source);
  if (m_view == TodoViewKind::List) {
    m_selectedListKey = m_document.m_lists.isEmpty() ? QString() : m_document.m_lists.first().m_key;
  }
  m_ready = true;
  m_readOnly = readOnly || !QFileInfo(canonical).isWritable();
  m_error.clear();
  if (!m_document.isValid()) {
    const auto &diagnostic = m_document.m_diagnostics.first();
    m_error = tr("Line %1: %2").arg(diagnostic.m_line).arg(diagnostic.m_message);
  }
  const auto binding = m_generation;
  config->getSessionConfig().setTodoStickerFilePath(canonical);
  if (!self || binding != m_generation) {
    return false;
  }
  publishModel();
  emit stateChanged();
  return true;
}

bool TodoStickerController::canEditTasks() const {
  return m_ready && !m_readOnly && m_document.isValid();
}

int TodoStickerController::addTask(const QString &p_title, const QString &p_destinationKey,
                                   const QDate &p_due) {
  if (!canEditTasks() || isBusy()) {
    return 0;
  }
  TodoTaskFields fields;
  fields.m_title = p_title.trimmed();
  if (fields.m_title.isEmpty()) {
    return 0;
  }
  QString destination = p_destinationKey;
  if (m_view == TodoViewKind::List) {
    if (!destination.isEmpty() && destination != m_selectedListKey) {
      reportError(tr("Choose an existing destination list."));
      return 0;
    }
    destination = m_selectedListKey;
  } else if (destination.isEmpty() && m_document.m_lists.size() == 1) {
    destination = m_document.m_lists.first().m_key;
  }
  if ((destination.isEmpty() && !m_document.m_lists.isEmpty()) ||
      (!destination.isEmpty() && !m_document.findList(destination))) {
    reportError(tr("Choose an existing destination list."));
    return 0;
  }
  switch (m_view) {
  case TodoViewKind::List:
    break;
  case TodoViewKind::MyDay:
    fields.m_myDay = m_today;
    break;
  case TodoViewKind::Important:
    fields.m_important = true;
    break;
  case TodoViewKind::Planned:
    if (!p_due.isValid()) {
      reportError(tr("Choose a due date for the new task."));
      return 0;
    }
    fields.m_due = p_due;
    break;
  case TodoViewKind::All:
    if (!m_dueDate.isValid()) {
      reportError(tr("Choose a due date for the new task."));
      return 0;
    }
    fields.m_due = m_dueDate;
    break;
  }
  return applyTaskEdit(destination, false, std::move(fields));
}

void TodoStickerController::setChecked(const QString &p_key, bool p_checked, int p_revision) {
  if (!canEditTasks() || isBusy()) {
    return;
  }
  if (p_revision != m_revision || p_revision != m_taskModel->getRevision()) {
    reportError(tr("This task is out of date. Refresh before trying again."));
    return;
  }
  const auto *item = m_document.findItem(p_key);
  if (!item || m_taskModel->rowForKey(p_key) < 0) {
    reportError(tr("The selected task no longer exists."));
    return;
  }
  if (item->m_fields.m_checked == p_checked) {
    return;
  }
  applyTaskEdit(p_key, p_checked, std::nullopt);
}

int TodoStickerController::applyTaskEdit(const QString &p_key, bool p_checked,
                                         std::optional<TodoTaskFields> p_newTask) {
  // Model-reset notifications below invalidate row references, including p_key.
  const QString key = p_key;
  const QString expected = m_document.m_source;
  const auto node = m_node;
  const auto generation = m_generation;
  const bool adding = p_newTask.has_value();
  const QPointer<TodoStickerController> self(this);
  auto *buffers = m_services.get<BufferService>();
  auto *services = &m_services;
  m_pendingListKey.clear();
  m_pendingTaskKey.clear();
  m_editing = true;
  publishModel();
  emit stateChanged();
  if (!self || generation != m_generation || !m_editing) {
    return 0;
  }
  const int token = buffers->editTextSnapshot(
      node, expected,
      [self, services, generation, key, p_checked,
       newTask = std::move(p_newTask)](const Buffer2 &p_buffer, const QString &p_source,
                                       QString *p_replacement, QString *p_error) {
        if (!self || self->m_generation != generation) {
          *p_error = tr("The TODO selection changed before the operation could finish.");
          return false;
        }
        // The accepted source and fields must outlive updateTask's reparsing.
        const QString accepted = p_source;
        QTextDocument document;
        document.setPlainText(accepted);
        if (document.toPlainText() != accepted) {
          *p_error = tr("The TODO source cannot be edited without changing unrelated text.");
          return false;
        }
        TodoViewWindowController controller(*services, &document, p_buffer);
        controller.setEditable(true);
        QString rejection;
        QObject::connect(&controller, &TodoViewWindowController::operationRejected, &controller,
                         [&rejection](const QString &p_message) { rejection = p_message; });
        if (newTask) {
          controller.setView(TodoViewKind::List, key);
          if (!controller.addTask(key, *newTask)) {
            *p_error = rejection.isEmpty() ? tr("The task could not be added.") : rejection;
            return false;
          }
          const auto *created = controller.getDocument().findItem(controller.getSelectedItemKey());
          if (!created) {
            *p_error = tr("The task could not be added.");
            return false;
          }
          self->m_pendingTaskKey = created->m_key;
          self->m_pendingListKey = created->m_listKey;
        } else {
          const auto *current = controller.getDocument().findItem(key);
          if (!current) {
            *p_error = tr("The selected task no longer exists.");
            return false;
          }
          auto fields = current->m_fields;
          controller.setView(TodoViewKind::List, current->m_listKey);
          fields.m_checked = p_checked;
          if (!controller.updateTask(key, fields)) {
            *p_error = rejection.isEmpty() ? tr("The task could not be updated.") : rejection;
            return false;
          }
          // The first structured edit can replace a source-position list key with an ID.
          self->m_pendingListKey = controller.getProjection().m_listKey;
        }
        *p_replacement = document.toPlainText();
        return true;
      },
      [self, generation, adding](bool p_saved, const QString &p_error) {
        if (!self || self->m_generation != generation) {
          return;
        }
        const int token = self->m_editToken;
        const auto taskKey = self->m_pendingTaskKey;
        const auto listKey = self->m_pendingListKey;
        self->m_editToken = 0;
        self->m_editing = false;
        self->m_pendingTaskKey.clear();
        self->m_pendingListKey.clear();
        self->refresh();
        if (!self || self->m_generation != generation) {
          return;
        }
        // An installed but unsaved task must consume the input to avoid duplicate retries.
        const bool created = !taskKey.isEmpty() && self->m_document.findItem(taskKey);
        if (self->m_view == TodoViewKind::List) {
          self->setView(TodoViewKind::List, listKey);
        }
        if (self && self->m_generation == generation && (!p_saved || !p_error.isEmpty())) {
          self->reportError(p_error.isEmpty() ? tr("The TODO change could not be saved.")
                                              : p_error);
        }
        if (adding && self && self->m_generation == generation) {
          emit self->taskAdditionFinished(token, created);
        }
      });
  if (!self || generation != m_generation) {
    if (token) {
      buffers->cancelSearchReplacement(token);
    }
    return 0;
  }
  m_editToken = token;
  if (!token) {
    m_editing = false;
    publishModel();
    reportError(tr("The TODO change could not start. Refresh and try again."));
  }
  return token;
}

void TodoStickerController::openTask(const QString &p_key) {
  if (!m_ready || isBusy()) {
    return;
  }
  const QString key = p_key;
  const QString expected = m_document.m_source;
  const auto generation = m_generation;
  const QPointer<TodoStickerController> self(this);
  refresh();
  if (!self || !m_ready || generation != m_generation) {
    return;
  }
  if (expected != m_document.m_source) {
    reportError(tr("The TODO source changed. Select the task again in the refreshed list."));
    return;
  }
  const auto *item = m_document.findItem(key);
  if (!item) {
    reportError(tr("The selected task no longer exists."));
    return;
  }
  FileOpenSettings settings;
  settings.m_alwaysNewWindow = true;
  settings.m_useInternalViewer = true;
  settings.m_lineNumber = m_document.lineAt(item->m_range.m_start) - 1;
  const auto opened = m_services.get<BufferService>()->openBuffer(m_node, settings);
  if (self && !opened.isValid()) {
    reportError(tr("The TODO file could not be opened."));
  }
}

void TodoStickerController::edit() {
  if (!m_ready || isBusy()) {
    return;
  }
  const QPointer<TodoStickerController> self(this);
  const auto generation = m_generation;
  refresh();
  if (!self || !m_ready || generation != m_generation) {
    return;
  }
  FileOpenSettings settings;
  settings.m_alwaysNewWindow = true;
  settings.m_useInternalViewer = true;
  settings.m_forceMode = true;
  settings.m_mode = ViewWindowMode::Edit;
  const auto opened = m_services.get<BufferService>()->openBuffer(m_node, settings);
  if (self && !opened.isValid()) {
    reportError(tr("The TODO file could not be opened for editing."));
  }
}

void TodoStickerController::retarget(const QString &p_notebookId, const QString &p_oldPath,
                                     const QString &p_newNotebookId, const QString &p_newPath,
                                     bool p_folder) {
  auto *notebooks = m_services.get<NotebookCoreService>();
  auto *config = m_services.get<ConfigMgr2>();
  if (!notebooks || !config) {
    return;
  }
  const auto oldAbsolute = notebooks->buildAbsolutePath(p_notebookId, p_oldPath);
  const auto chosen = config->getSessionConfig().getTodoStickerFilePath();
  if (oldAbsolute.isEmpty() || !containsPath(oldAbsolute, chosen, p_folder)) {
    return;
  }
  const auto newAbsolute = notebooks->buildAbsolutePath(p_newNotebookId, p_newPath);
  if (!newAbsolute.isEmpty()) {
    config->getSessionConfig().setTodoStickerFilePath(newAbsolute + chosen.mid(oldAbsolute.size()));
  }
}
