#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QPointer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>
#include <core/configmgr2.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/searchcoreservice.h>
#include <core/services/searchservice.h>
#include <core/services/syncworkqueuemanager.h>
#include <integrations/httpapi/vnoteapibackend.h>
#include <stdexcept>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace {
bool writeFile(const QString &path, const QByteArray &text) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text) == text.size();
}
QByteArray readFile(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Isolated real context/services. Destruction stops the backend before buffers/core disappear.
struct Fixture {
  QTemporaryDir directory;
  VxCoreContextHandle context = nullptr;
  ServiceLocator services;
  NotebookIoGate gate;
  HookManager hooks;
  SyncWorkQueueManager queues;
  std::unique_ptr<ConfigCoreService> configCore;
  std::unique_ptr<ConfigMgr2> config;
  std::unique_ptr<NotebookCoreService> notebooks;
  std::unique_ptr<FileTypeCoreService> types;
  std::unique_ptr<BufferService> buffers;
  std::unique_ptr<SearchCoreService> searchCore;
  std::unique_ptr<SearchService> search;
  std::unique_ptr<VNoteApiBackend> backend;
  QString notebookId;
  QMap<QByteArray, QByteArray> previousTemp;
  QSet<QByteArray> presentTemp;
  bool tempChanged = false;

  bool init(NotebookType p_type = NotebookType::Bundled) {
    if (!directory.isValid())
      return false;
    // Test mode otherwise shares one vxcore_test_config directory with other
    // test executables. Keep configuration under this fixture as well as notes.
    for (const auto &key : {QByteArray("TMP"), QByteArray("TEMP"), QByteArray("TMPDIR")}) {
      if (qEnvironmentVariableIsSet(key.constData()))
        presentTemp.insert(key);
      previousTemp.insert(key, qgetenv(key.constData()));
      qputenv(key.constData(), QFile::encodeName(directory.path()));
    }
    tempChanged = true;
    if (!initServices())
      return false;
    const auto root = directory.filePath(QStringLiteral("notes"));
    if (p_type == NotebookType::Raw && !QDir().mkpath(root))
      return false;
    notebookId =
        notebooks->createNotebook(root, QStringLiteral("{\"name\":\"API fixture\"}"), p_type);
    return !notebookId.isEmpty();
  }
  bool initServices() {
    vxcore_set_test_mode(1);
    if (vxcore_context_create(nullptr, &context) != VXCORE_OK)
      return false;
    if (vxcore_context_update_config(context, "{\"search\":{\"backends\":[\"simple\"]}}") !=
        VXCORE_OK)
      return false;
    configCore = std::make_unique<ConfigCoreService>(context);
    config = std::make_unique<ConfigMgr2>(configCore.get());
    config->init();
    notebooks = std::make_unique<NotebookCoreService>(context);
    notebooks->setHookManager(&hooks);
    notebooks->setNotebookIoGate(&gate);
    types = std::make_unique<FileTypeCoreService>(context, QStringLiteral("en_US"));
    buffers = std::make_unique<BufferService>(context, &hooks, &gate, AutoSavePolicy::None);
    buffers->setSyncWorkQueueManager(&queues);
    searchCore = std::make_unique<SearchCoreService>(context);
    search = std::make_unique<SearchService>(searchCore.get());
    services.registerService(&hooks);
    services.registerService(&gate);
    services.registerService(&queues);
    services.registerService(configCore.get());
    services.registerService(config.get());
    services.registerService(notebooks.get());
    services.registerService(types.get());
    services.registerService(buffers.get());
    services.registerService(searchCore.get());
    services.registerService(search.get());
    backend = std::make_unique<VNoteApiBackend>(services);
    return true;
  }
  bool restart() {
    releaseServices();
    return initServices();
  }
  void releaseServices() {
    backend.reset();
    search.reset();
    searchCore.reset();
    buffers.reset();
    types.reset();
    notebooks.reset();
    config.reset();
    configCore.reset();
    if (context) {
      vxcore_context_destroy(context);
      context = nullptr;
    }
  }
  ~Fixture() {
    releaseServices();
    if (tempChanged) {
      for (auto it = previousTemp.cbegin(); it != previousTemp.cend(); ++it) {
        if (presentTemp.contains(it.key()))
          qputenv(it.key().constData(), it.value());
        else
          qunsetenv(it.key().constData());
      }
    }
  }
  QString add(const QString &name, const QByteArray &bytes = {}) {
    const auto id = notebooks->createFile(notebookId, {}, name);
    if (id.isEmpty() || !writeFile(notebooks->buildAbsolutePath(notebookId, name), bytes))
      return {};
    return id;
  }
  ApiRequestContext principal() const {
    ApiRequestContext result;
    result.requestId = QStringLiteral("fixture-request");
    result.principal.tokenId = QStringLiteral("fixture-token");
    result.principal.notebookIds = {notebookId};
    result.principal.scopes = {QStringLiteral("notes:read"), QStringLiteral("notes:read-live"),
                               QStringLiteral("notes:write"), QStringLiteral("notes:trash")};
    result.authorized = [] { return true; };
    result.cancelled = std::make_shared<std::atomic_bool>(false);
    return result;
  }
  ApiCommand command(ApiCommandKind kind, const QString &node = {}) const {
    ApiCommand result;
    result.kind = kind;
    result.notebookId = notebookId;
    result.nodeId = node;
    result.limit = kind == ApiCommandKind::ReadContent ? 65536 : 50;
    return result;
  }
  ApiResult execute(ApiCommand command, ApiRequestContext request) {
    struct CompletionState {
      bool done = false;
      ApiResult result;
      QPointer<QEventLoop> loop;
    };
    auto state = std::make_shared<CompletionState>();
    QEventLoop loop;
    state->loop = &loop;
    backend->execute(std::move(command), std::move(request), [state](ApiResult result) {
      state->result = std::move(result);
      state->done = true;
      if (state->loop)
        state->loop->quit();
    });
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(15000);
    if (!state->done)
      loop.exec();
    if (!state->done)
      state->result.error = {599, QStringLiteral("test_timeout"),
                             QStringLiteral("Backend did not complete")};
    return state->result;
  }
  ApiResult execute(ApiCommand command) { return execute(std::move(command), principal()); }
};

void notebookTypes() {
  QTest::addColumn<int>("notebookType");
  QTest::newRow("bundled") << static_cast<int>(NotebookType::Bundled);
  QTest::newRow("raw") << static_cast<int>(NotebookType::Raw);
}
} // namespace

class TestVNoteApiBackend : public QObject {
  Q_OBJECT
private slots:
  void savedAndLiveAreDifferentAndNonmutating_data() { notebookTypes(); }
  void savedAndLiveAreDifferentAndNonmutating() {
    QFETCH(int, notebookType);
    Fixture fixture;
    QVERIFY(fixture.init(static_cast<NotebookType>(notebookType)));
    const auto id = fixture.add(QStringLiteral("draft.md"), QByteArrayLiteral("saved\r\n"));
    QVERIFY(!id.isEmpty());
    auto buffer = fixture.buffers->openBuffer({fixture.notebookId, QStringLiteral("draft.md")});
    QVERIFY(buffer.isValid());
    const auto raw = buffer.getContentRaw();
    QString draft = QStringLiteral("live \u03bb\n");
    fixture.buffers->registerActiveWriter(buffer.id(), 1, [&] { return draft; });
    fixture.buffers->markDirty(buffer.id());
    const auto saved = fixture.execute(fixture.command(ApiCommandKind::ReadContent, id));
    QCOMPARE(saved.error.status, 0);
    QCOMPARE(saved.data.value("text").toString(), QStringLiteral("saved\r\n"));
    QCOMPARE(saved.data.value("view").toString(), QStringLiteral("saved"));
    QVERIFY(saved.data.value("dirty").toBool());
    QVERIFY(saved.data.contains("noteStateVersion"));
    auto liveCommand = fixture.command(ApiCommandKind::ReadContent, id);
    liveCommand.view = ApiContentView::Live;
    const auto live = fixture.execute(liveCommand);
    QCOMPARE(live.error.status, 0);
    QCOMPARE(live.data.value("text").toString(), draft);
    QCOMPARE(live.data.value("view").toString(), QStringLiteral("live"));
    QVERIFY(!live.data.contains("noteStateVersion"));
    QVERIFY(live.data.value("contentVersion") != saved.data.value("contentVersion"));
    QCOMPARE(buffer.getContentRaw(), raw);
    QVERIFY(fixture.buffers->isDirty(buffer.id()));
    QCOMPARE(readFile(fixture.notebooks->buildAbsolutePath(fixture.notebookId,
                                                           QStringLiteral("draft.md"))),
             raw);
    fixture.buffers->unregisterActiveWriter(buffer.id(), 1);
    const auto fallback = fixture.execute(liveCommand);
    QCOMPARE(fallback.data.value("view").toString(), QStringLiteral("saved"));
    QCOMPARE(fallback.data.value("text").toString(), QStringLiteral("saved\r\n"));
    fixture.buffers->registerActiveWriter(
        buffer.id(), 4, []() -> QString { throw std::runtime_error("private editor error"); });
    const auto failedCapture = fixture.execute(liveCommand);
    QCOMPARE(failedCapture.error.code, QStringLiteral("live_capture_failed"));
    QVERIFY(failedCapture.data.isEmpty());
    QVERIFY(!failedCapture.error.message.contains(QStringLiteral("private editor error")));
    fixture.buffers->unregisterActiveWriter(buffer.id(), 4);
  }

  void bytePaginationAndSnapshotConflicts() {
    Fixture fixture;
    QVERIFY(fixture.init());
    const auto id =
        fixture.add(QStringLiteral("unicode.md"), QStringLiteral("A\u03bb\U0001f600Z").toUtf8());
    QVERIFY(!id.isEmpty());
    auto command = fixture.command(ApiCommandKind::ReadContent, id);
    command.limit = 4;
    const auto first = fixture.execute(command);
    QCOMPARE(first.error.status, 0);
    QCOMPARE(first.data.value("text").toString(), QStringLiteral("A\u03bb"));
    QCOMPARE(first.data.value("returnedBytes").toInt(), 3);
    command.cursor = first.data.value("nextCursor").toString();
    command.offset = 3;
    const auto second = fixture.execute(command);
    QCOMPARE(second.error.status, 0);
    QCOMPARE(second.data.value("text").toString(), QStringLiteral("\U0001f600"));
    command.cursor.clear();
    command.offset = 2;
    QCOMPARE(fixture.execute(command).error.code, QStringLiteral("invalid_offset"));
    command.cursor = first.data.value("nextCursor").toString();
    command.offset = 3;
    QVERIFY(writeFile(
        fixture.notebooks->buildAbsolutePath(fixture.notebookId, QStringLiteral("unicode.md")),
        QByteArrayLiteral("changed")));
    QCOMPARE(fixture.execute(command).error.status, 412);
  }

  void grantsAndProtectedNotesDoNotLeak() {
    Fixture fixture;
    QVERIFY(fixture.init());
    const auto visible = fixture.add(QStringLiteral("visible.md"), QByteArrayLiteral("needle"));
    QVERIFY(!visible.isEmpty());
    auto setup = fixture.notebooks->prepareNotebookEncryption(
        fixture.notebookId, {}, QByteArrayLiteral("http-api-protected-test"));
    QVERIFY(setup.isValid());
    QString protectedId;
    {
      auto maintenance = fixture.queues.tryAcquireMaintenance({fixture.notebookId});
      QVERIFY(maintenance.isValid());
      NotebookIoGate::ScopedTryLock lock(fixture.gate, fixture.notebookId, 0);
      QVERIFY(lock.isLocked());
      QCOMPARE(fixture.notebooks->commitNotebookEncryption(setup), VXCORE_OK);
      QCOMPARE(fixture.notebooks->createEncryptedNote(
                   fixture.notebookId, {}, QStringLiteral("secret-title.md"),
                   QStringLiteral("markdown"), QByteArrayLiteral("secret-body needle"),
                   &protectedId),
               VXCORE_OK);
    }
    auto protectedBuffer = fixture.buffers->openBufferByNodeId(protectedId);
    QVERIFY(protectedBuffer.isValid());
    fixture.buffers->registerActiveWriter(protectedBuffer.id(), 2,
                                          [] { return QStringLiteral("unlocked-secret"); });
    const auto listing = fixture.execute(fixture.command(ApiCommandKind::ListNodes));
    QCOMPARE(listing.error.status, 0);
    QCOMPARE(listing.data.value("items").toArray().size(), 1);
    QVERIFY(!QJsonDocument(listing.data).toJson().contains("secret"));
    auto direct = fixture.command(ApiCommandKind::ReadContent, protectedId);
    direct.view = ApiContentView::Live;
    const auto denied = fixture.execute(direct);
    QCOMPARE(denied.error.code, QStringLiteral("protected_note_unavailable"));
    QVERIFY(denied.data.isEmpty());
    auto search = fixture.command(ApiCommandKind::Search);
    search.query = QStringLiteral("needle");
    const auto results = fixture.execute(search);
    QCOMPARE(results.error.status, 0);
    QCOMPARE(results.data.value("items").toArray().size(), 1);
    QVERIFY(!QJsonDocument(results.data).toJson().contains("secret"));
    auto principal = fixture.principal();
    principal.principal.notebookIds.clear();
    QCOMPARE(
        fixture.execute(fixture.command(ApiCommandKind::ReadNote, visible), principal).error.status,
        404);
    QCOMPARE(fixture.execute(fixture.command(ApiCommandKind::ListNotebooks), principal)
                 .data.value("items")
                 .toArray()
                 .size(),
             0);
    search.notebookIds = {fixture.notebookId};
    QCOMPARE(fixture.execute(search, principal).error.status, 404);
    fixture.buffers->unregisterActiveWriter(protectedBuffer.id(), 2);
  }

  void specializedMetadataAndLosslessEncoding() {
    Fixture fixture;
    QVERIFY(fixture.init());
    const auto binary = fixture.add(QStringLiteral("drawing.pdf"), QByteArrayLiteral("%PDF-test"));
    QVERIFY(!binary.isEmpty());
    const auto metadata = fixture.execute(fixture.command(ApiCommandKind::ReadNote, binary));
    QCOMPARE(metadata.error.status, 0);
    QVERIFY(!metadata.data.value("contentReadable").toBool());
    QVERIFY(!metadata.data.value("capabilities").toObject().value("trash").toBool());
    QCOMPARE(fixture.execute(fixture.command(ApiCommandKind::ReadContent, binary)).error.status,
             422);
    const auto malformed = fixture.add(QStringLiteral("invalid.md"), QByteArray::fromHex("c328"));
    QVERIFY(!malformed.isEmpty());
    QCOMPARE(fixture.execute(fixture.command(ApiCommandKind::ReadContent, malformed)).error.code,
             QStringLiteral("unsupported_encoding"));
    const auto utf8 = fixture.add(QStringLiteral("ordinary.md"), QByteArrayLiteral("hello"));
    auto principal = fixture.principal();
    principal.principal.scopes = {QStringLiteral("notes:read")};
    auto live = fixture.command(ApiCommandKind::ReadContent, utf8);
    live.view = ApiContentView::Live;
    QCOMPARE(fixture.execute(live, principal).error.status, 403);
  }

  void mutationsUseVersionsAndPersistentIds_data() { notebookTypes(); }
  void mutationsUseVersionsAndPersistentIds() {
    QFETCH(int, notebookType);
    Fixture fixture;
    QVERIFY(fixture.init(static_cast<NotebookType>(notebookType)));
    auto create = fixture.command(ApiCommandKind::CreateNote);
    create.name = QStringLiteral("new.md");
    create.format = QStringLiteral("markdown");
    const auto created = fixture.execute(create);
    QCOMPARE(created.error.status, 0);
    QVERIFY(created.committed);
    QVERIFY(created.saved);
    const auto id = created.data.value("id").toString();
    QVERIFY(!id.isEmpty());
    const auto empty = fixture.execute(fixture.command(ApiCommandKind::ReadContent, id));
    QCOMPARE(empty.error.status, 0);
    QCOMPARE(empty.data.value("text").toString(), QString());
    auto replace = fixture.command(ApiCommandKind::ReplaceContent, id);
    replace.expectedVersion = empty.data.value("contentVersion").toString();
    replace.text = QStringLiteral("one\n");
    const auto replaced = fixture.execute(replace);
    QCOMPARE(replaced.error.status, 0);
    QVERIFY(replaced.committed && replaced.saved);
    QVERIFY(!replaced.data.value("contentVersion").toString().isEmpty());
    QCOMPARE(fixture.execute(replace).error.status, 412);
    auto append = fixture.command(ApiCommandKind::AppendContent, id);
    append.expectedVersion = replaced.data.value("contentVersion").toString();
    append.text = QStringLiteral("two");
    const auto appended = fixture.execute(append);
    QCOMPARE(appended.error.status, 0);
    QVERIFY(appended.saved);
    const auto metadata = fixture.execute(fixture.command(ApiCommandKind::ReadNote, id));
    auto rename = fixture.command(ApiCommandKind::RenameNote, id);
    rename.name = QStringLiteral("renamed.md");
    rename.expectedVersion = metadata.data.value("metadataVersion").toString();
    const auto renamed = fixture.execute(rename);
    QCOMPARE(renamed.error.status, 0);
    QVERIFY(renamed.committed);
    QCOMPARE(renamed.data.value("id").toString(), id);
    const auto contents = fixture.execute(fixture.command(ApiCommandKind::ReadContent, id));
    QCOMPARE(contents.data.value("text").toString(), QStringLiteral("one\ntwo"));
    auto trash = fixture.command(ApiCommandKind::TrashNote, id);
    const bool supportsTrash = notebookType == static_cast<int>(NotebookType::Bundled);
    QCOMPARE(metadata.data.value("capabilities").toObject().value("trash").toBool(), supportsTrash);
    if (!supportsTrash) {
      trash.expectedVersion = contents.data.value("noteStateVersion").toString();
      const auto rejected = fixture.execute(trash);
      QCOMPARE(rejected.error.status, 422);
      QCOMPARE(rejected.error.code, QStringLiteral("unsupported_trash"));
      QVERIFY(!rejected.committed && !rejected.saved);
      const auto retained = fixture.execute(fixture.command(ApiCommandKind::ReadContent, id));
      QCOMPARE(retained.error.status, 0);
      QCOMPARE(retained.data.value("id").toString(), id);
      QCOMPARE(retained.data.value("contentVersion"), contents.data.value("contentVersion"));
      QCOMPARE(readFile(fixture.directory.filePath(QStringLiteral("notes/renamed.md"))),
               QByteArrayLiteral("one\ntwo"));
      return;
    }
    trash.expectedVersion = contents.data.value("contentVersion").toString();
    QCOMPARE(fixture.execute(trash).error.status, 412);
    trash.expectedVersion = contents.data.value("noteStateVersion").toString();
    const auto trashed = fixture.execute(trash);
    QCOMPARE(trashed.error.status, 0);
    QVERIFY(trashed.committed);
    QCOMPARE(fixture.execute(fixture.command(ApiCommandKind::ReadNote, id)).error.status, 404);
  }

  void dirtyDraftAndExternalChangesRefuseWrites_data() { notebookTypes(); }
  void dirtyDraftAndExternalChangesRefuseWrites() {
    QFETCH(int, notebookType);
    Fixture fixture;
    QVERIFY(fixture.init(static_cast<NotebookType>(notebookType)));
    const auto id = fixture.add(QStringLiteral("guard.md"), QByteArrayLiteral("original"));
    const auto initial = fixture.execute(fixture.command(ApiCommandKind::ReadContent, id));
    auto buffer = fixture.buffers->openBuffer({fixture.notebookId, QStringLiteral("guard.md")});
    QVERIFY(buffer.isValid());
    fixture.buffers->registerActiveWriter(buffer.id(), 3, [] { return QStringLiteral("draft"); });
    fixture.buffers->markDirty(buffer.id());
    auto replace = fixture.command(ApiCommandKind::ReplaceContent, id);
    replace.text = QStringLiteral("replacement");
    replace.expectedVersion = initial.data.value("contentVersion").toString();
    const auto dirty = fixture.execute(replace);
    QCOMPARE(dirty.error.code, QStringLiteral("unsaved_changes"));
    QVERIFY(!dirty.committed && !dirty.saved);
    QCOMPARE(readFile(fixture.notebooks->buildAbsolutePath(fixture.notebookId,
                                                           QStringLiteral("guard.md"))),
             QByteArrayLiteral("original"));
    fixture.buffers->unregisterActiveWriter(buffer.id(), 3);
    QVERIFY(fixture.buffers->closeBuffer(buffer.id()));
    QVERIFY(writeFile(
        fixture.notebooks->buildAbsolutePath(fixture.notebookId, QStringLiteral("guard.md")),
        QByteArrayLiteral("external")));
    QCOMPARE(fixture.execute(replace).error.status, 412);
  }

  void rawDiscoveryAndNestedIdentitySurviveContextRestart() {
    Fixture fixture;
    QVERIFY(fixture.init(NotebookType::Raw));
    const auto nestedPath = fixture.directory.filePath(QStringLiteral("notes/nested"));
    QVERIFY(QDir().mkpath(nestedPath));
    const auto externalPath = QDir(nestedPath).filePath(QStringLiteral("external.txt"));
    const auto bytes = QByteArrayLiteral("raw-content-needle\n");
    QVERIFY(writeFile(externalPath, bytes));
    auto itemNamed = [](const ApiResult &result, const QString &name) {
      for (const auto &entry : result.data.value("items").toArray()) {
        const auto item = entry.toObject();
        if (item.value("name").toString() == name)
          return item;
      }
      return QJsonObject();
    };
    auto listing = fixture.command(ApiCommandKind::ListNodes);
    const auto root = fixture.execute(listing);
    QCOMPARE(root.error.status, 0);
    const auto folderId = itemNamed(root, QStringLiteral("nested")).value("id").toString();
    QVERIFY(!folderId.isEmpty());
    listing.parentId = folderId;
    const auto children = fixture.execute(listing);
    QCOMPARE(children.error.status, 0);
    const auto fileId = itemNamed(children, QStringLiteral("external.txt")).value("id").toString();
    QVERIFY(!fileId.isEmpty());
    const auto saved = fixture.execute(fixture.command(ApiCommandKind::ReadContent, fileId));
    QCOMPARE(saved.error.status, 0);
    QCOMPARE(saved.data.value("text").toString().toUtf8(), bytes);
    for (bool content : {false, true}) {
      auto search = fixture.command(ApiCommandKind::Search);
      search.searchContent = content;
      search.query = content ? QStringLiteral("raw-content-needle") : QStringLiteral("external");
      const auto found = fixture.execute(search);
      QCOMPARE(found.error.status, 0);
      QCOMPARE(itemNamed(found, QStringLiteral("external.txt")).value("id").toString(), fileId);
    }
    auto create = fixture.command(ApiCommandKind::CreateNote);
    create.parentId = folderId;
    create.name = QStringLiteral("created.txt");
    create.format = QStringLiteral("text");
    const auto created = fixture.execute(create);
    QCOMPARE(created.error.status, 0);
    QVERIFY(created.committed && created.saved);
    const auto createdId = created.data.value("id").toString();
    QVERIFY(!createdId.isEmpty());
    QCOMPARE(fixture.notebooks->getNodePathById(fixture.notebookId, createdId),
             QStringLiteral("nested/created.txt"));
    QVERIFY(QFileInfo::exists(QDir(nestedPath).filePath(QStringLiteral("created.txt"))));
    const auto metadata = fixture.execute(fixture.command(ApiCommandKind::ReadNote, fileId));
    auto rename = fixture.command(ApiCommandKind::RenameNote, fileId);
    rename.name = QStringLiteral("renamed.txt");
    rename.expectedVersion = metadata.data.value("metadataVersion").toString();
    const auto renamed = fixture.execute(rename);
    QCOMPARE(renamed.error.status, 0);
    QCOMPARE(renamed.data.value("id").toString(), fileId);
    QVERIFY(!QFileInfo::exists(externalPath));
    QCOMPARE(readFile(QDir(nestedPath).filePath(QStringLiteral("renamed.txt"))), bytes);

    const auto notebookId = fixture.notebookId;
    QVERIFY(fixture.restart());
    QCOMPARE(fixture.notebooks->getNotebookConfig(notebookId).value("id").toString(), notebookId);
    const auto restoredRoot = fixture.execute(fixture.command(ApiCommandKind::ListNodes));
    QCOMPARE(restoredRoot.error.status, 0);
    QCOMPARE(itemNamed(restoredRoot, QStringLiteral("nested")).value("id").toString(), folderId);
    const auto restoredChildren = fixture.execute(listing);
    QCOMPARE(restoredChildren.error.status, 0);
    QCOMPARE(itemNamed(restoredChildren, QStringLiteral("renamed.txt")).value("id").toString(),
             fileId);
    QCOMPARE(itemNamed(restoredChildren, QStringLiteral("created.txt")).value("id").toString(),
             createdId);
    const auto restored = fixture.execute(fixture.command(ApiCommandKind::ReadContent, fileId));
    QCOMPARE(restored.error.status, 0);
    QCOMPARE(restored.data.value("text").toString().toUtf8(), bytes);
  }

  void listingCursorRejectsRename() {
    Fixture fixture;
    QVERIFY(fixture.init());
    const auto first = fixture.add(QStringLiteral("one.md"));
    const auto second = fixture.add(QStringLiteral("two.md"));
    QVERIFY(!first.isEmpty() && !second.isEmpty());
    auto command = fixture.command(ApiCommandKind::ListNodes);
    command.limit = 1;
    command.parentId =
        fixture.notebooks->getFileInfo(fixture.notebookId, QString()).value("id").toString();
    QVERIFY(!command.parentId.isEmpty());
    const auto listing = fixture.execute(command);
    QCOMPARE(listing.error.status, 0);
    QVERIFY(listing.data.value("nextCursor").isString());
    QVERIFY(fixture.notebooks->renameFile(fixture.notebookId, QStringLiteral("two.md"),
                                          QStringLiteral("changed.md")));
    command.cursor = listing.data.value("nextCursor").toString();
    QCOMPARE(fixture.execute(command).error.status, 412);
    const auto metadata = fixture.execute(fixture.command(ApiCommandKind::ReadNote, second));
    QCOMPARE(metadata.data.value("id").toString(), second);
    QCOMPARE(metadata.data.value("path").toString(), QStringLiteral("changed.md"));
  }

  void excludesProfileAndLinkedFiles() {
    Fixture fixture;
    QVERIFY(fixture.init());
    const auto controlBook = fixture.notebooks->createNotebook(
        QDir(fixture.config->getUserConfigPath()).filePath(QStringLiteral("api-control-fixture")),
        QStringLiteral("{\"name\":\"Control\"}"), NotebookType::Bundled);
    QVERIFY(!controlBook.isEmpty());
    const auto control =
        fixture.notebooks->createFile(controlBook, {}, QStringLiteral("control.md"));
    QVERIFY(!control.isEmpty());
    auto principal = fixture.principal();
    principal.principal.notebookIds.insert(controlBook);
    auto command = fixture.command(ApiCommandKind::ReadNote, control);
    command.notebookId = controlBook;
    QCOMPARE(fixture.execute(command, principal).error.status, 404);
#ifndef Q_OS_WIN
    const auto id = fixture.add(QStringLiteral("linked.md"), QByteArrayLiteral("original"));
    const auto path =
        fixture.notebooks->buildAbsolutePath(fixture.notebookId, QStringLiteral("linked.md"));
    const auto outside = fixture.directory.filePath(QStringLiteral("outside.txt"));
    QVERIFY(writeFile(outside, QByteArrayLiteral("outside secret")));
    QVERIFY(QFile::remove(path));
    QVERIFY(QFile::link(outside, path));
    QCOMPARE(fixture.execute(fixture.command(ApiCommandKind::ReadContent, id)).error.status, 404);
#endif
  }

  void sharedSearchOwnershipCapacityAndStop() {
    Fixture fixture;
    QVERIFY(fixture.init());
    QVERIFY(!fixture.add(QStringLiteral("search.md"), QByteArrayLiteral("needle\n")).isEmpty());
    QSignalSpy guiFinished(fixture.search.get(), &SearchService::searchFinished);
    QSignalSpy guiCancelled(fixture.search.get(), &SearchService::searchCancelled);
    const int guiToken = fixture.search->searchContent(
        fixture.notebookId, QStringLiteral("{\"pattern\":\"needle\"}"),
        QStringLiteral("{\"files\":[\"search.md\"]}"));
    auto search = fixture.command(ApiCommandKind::Search);
    search.query = QStringLiteral("needle");
    auto completed = std::make_shared<QVector<ApiResult>>();
    for (int i = 0; i < 3; ++i)
      fixture.backend->execute(search, fixture.principal(), [completed](ApiResult result) {
        completed->append(std::move(result));
      });
    const auto overflow = fixture.execute(search);
    QCOMPARE(overflow.error.status, 429);
    fixture.backend->stop();
    QCOMPARE(completed->size(), 3); // Synchronous settlement, no GUI event needed to stop.
    for (const auto &result : *completed)
      QCOMPARE(result.error.status, 503);
    fixture.backend.reset();
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.search->isSearching(guiToken), 10000);
    bool ownFinished = false;
    for (const auto &args : guiFinished)
      ownFinished = ownFinished || args.first().toInt() == guiToken;
    QVERIFY(ownFinished);
    for (const auto &args : guiCancelled)
      QVERIFY(args.first().toInt() != guiToken);
    QCoreApplication::processEvents(); // Destroyed-backend read futures cannot call dangling state.
    QCOMPARE(completed->size(), 3);
  }

  void commitRechecksDiskAfterTheSnapshot() {
    Fixture fixture;
    QVERIFY(fixture.init());
    const auto id = fixture.add(QStringLiteral("raced.md"), QByteArrayLiteral("before"));
    QVERIFY(!id.isEmpty());
    const auto saved = fixture.execute(fixture.command(ApiCommandKind::ReadContent, id));
    QCOMPARE(saved.error.status, 0);
    const auto path =
        fixture.notebooks->buildAbsolutePath(fixture.notebookId, QStringLiteral("raced.md"));
    bool edited = false;
    const auto hook = fixture.hooks.addAction<BufferEvent>(
        HookNames::FileBeforeSave, [&](HookContext &, const BufferEvent &) {
          edited = writeFile(path, QByteArrayLiteral("external race"));
        });
    const auto removeHook = qScopeGuard([&] { fixture.hooks.removeAction(hook); });
    auto replace = fixture.command(ApiCommandKind::ReplaceContent, id);
    replace.expectedVersion = saved.data.value("contentVersion").toString();
    replace.text = QStringLiteral("must not overwrite");
    const auto result = fixture.execute(replace);
    QVERIFY(edited);
    QCOMPARE(result.error.code, QStringLiteral("precondition_failed"));
    QVERIFY(!result.saved);
    QCOMPARE(readFile(path), QByteArrayLiteral("external race"));
  }

  void revocationBeforeSavedPublication() {
    Fixture fixture;
    QVERIFY(fixture.init());
    const auto id = fixture.add(QStringLiteral("revoked.md"), QByteArrayLiteral("secret"));
    auto valid = std::make_shared<std::atomic_bool>(true);
    auto principal = fixture.principal();
    principal.authorized = [valid] { return valid->load(); };
    auto result = std::make_shared<ApiResult>();
    auto done = std::make_shared<bool>(false);
    fixture.backend->execute(fixture.command(ApiCommandKind::ReadContent, id), principal,
                             [result, done](ApiResult value) {
                               *result = std::move(value);
                               *done = true;
                             });
    valid->store(false);
    QTRY_VERIFY_WITH_TIMEOUT(*done, 10000);
    QCOMPARE(result->error.status, 503);
    QVERIFY(result->data.isEmpty());
  }
};

QTEST_GUILESS_MAIN(TestVNoteApiBackend)
#include "test_vnoteapibackend.moc"
