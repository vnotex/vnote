#include <QtTest>

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <api/apiauth.h>
#include <core/configmgr2.h>
#include <core/httpapiconfig.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/commentservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/notificationservice.h>
#include <core/services/searchcoreservice.h>
#include <core/services/searchservice.h>
#include <core/services/syncworkqueuemanager.h>
#include <core/sessionconfig.h>
#include <integrations/httpapi/apicredentialstore.h>
#include <integrations/httpapi/httpapicontroller.h>
#include <vxcore/vxcore.h>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <aclapi.h>
#include <sddl.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

using namespace vnotex;

namespace {
struct HttpResult {
  int status = 0;
  QByteArray body;
};

HttpResult request(const QString &p_endpoint, const QString &p_token = {}) {
  QNetworkAccessManager manager;
  QNetworkRequest query(QUrl(p_endpoint + QStringLiteral("/info")));
  if (!p_token.isEmpty()) {
    query.setRawHeader("Authorization", QByteArray("Bearer ") + p_token.toUtf8());
  }
  auto *reply = manager.get(query);
  QElapsedTimer elapsed;
  elapsed.start();
  while (!reply->isFinished() && elapsed.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QTest::qWait(1);
  }
  if (!reply->isFinished()) {
    reply->abort();
  }
  return {reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll()};
}

quint16 availablePort() {
  QTcpServer reserve;
  return reserve.listen(QHostAddress::LocalHost, 0) ? reserve.serverPort() : 0;
}

ApiTokenGrant grant() {
  ApiTokenGrant value;
  value.label = QStringLiteral("Lifecycle client");
  value.notebookIds = {QStringLiteral("fixture-notebook")};
  value.scopes = {QStringLiteral("notes:read")};
  value.expiresAt = QDateTime::currentDateTimeUtc().addDays(1);
  return value;
}
} // namespace

class TestHttpApiLifecycle : public QObject {
  Q_OBJECT
private slots:
  void absentStoreIsReadOnly();
  void credentialsPersistWithoutBearerSecrets();
  void unsafeOrCorruptStoreFailsClosed();
  void controllerOwnsStartupDisableAndRevocation();
};

void TestHttpApiLifecycle::absentStoreIsReadOnly() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto directory = temporary.filePath(QStringLiteral("http-api"));
  ApiCredentialStore store(directory);
  QJsonArray records;
  QString error;
  QVERIFY2(store.load(&records, &error), qPrintable(error));
  QVERIFY(records.isEmpty());
  QVERIFY(!QFileInfo::exists(directory));
}

void TestHttpApiLifecycle::credentialsPersistWithoutBearerSecrets() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto directory = temporary.filePath(QStringLiteral("http-api"));
  ApiIssuedToken token;
  QString error;
  {
    ApiCredentialStore store(directory);
    ApiAuth auth(store);
    QVERIFY2(auth.load(&error), qPrintable(error));
    QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Issued);
    QVERIFY(auth.authenticate(token.secret.toUtf8()).has_value());
  }
  QFile registry(QDir(directory).filePath(QStringLiteral("tokens.json")));
  QVERIFY(registry.open(QIODevice::ReadOnly));
  const auto bytes = registry.readAll();
  QVERIFY(!bytes.contains(token.secret.toUtf8()));
  const auto secretPart = token.secret.section(QLatin1Char('.'), 1).toUtf8();
  QVERIFY(!secretPart.isEmpty());
  QVERIFY(!bytes.contains(secretPart));
  registry.close();
  {
    ApiCredentialStore store(directory);
    ApiAuth auth(store);
    QVERIFY2(auth.load(&error), qPrintable(error));
    const auto principal = auth.authenticate(token.secret.toUtf8());
    QVERIFY(principal.has_value());
    QCOMPARE(principal->tokenId, token.id);
    QCOMPARE(principal->notebookIds, grant().notebookIds);
    QVERIFY2(auth.revoke(token.id, &error), qPrintable(error));
    QVERIFY(!auth.authenticate(token.secret.toUtf8()).has_value());
  }
  ApiCredentialStore reloadedStore(directory);
  ApiAuth reloaded(reloadedStore);
  QVERIFY2(reloaded.load(&error), qPrintable(error));
  QVERIFY(!reloaded.authenticate(token.secret.toUtf8()).has_value());
}

void TestHttpApiLifecycle::unsafeOrCorruptStoreFailsClosed() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto directory = temporary.filePath(QStringLiteral("http-api"));
  const auto path = QDir(directory).filePath(QStringLiteral("tokens.json"));
  ApiCredentialStore store(directory);
  ApiAuth auth(store);
  ApiIssuedToken token;
  QString error;
  QVERIFY2(auth.load(&error), qPrintable(error));
  QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Issued);

  QFile registry(path);
  QVERIFY(registry.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(registry.write("{broken"), qint64(7));
  registry.close();
  QJsonArray records;
  QVERIFY(!store.load(&records, &error));
  QCOMPARE(error, QStringLiteral("credential_store_corrupt"));
  QVERIFY(registry.open(QIODevice::ReadOnly));
  QCOMPARE(registry.readAll(), QByteArray("{broken"));
  registry.close();

#ifdef Q_OS_WIN
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  QVERIFY(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;WD)", SDDL_REVISION_1,
                                                               &descriptor, nullptr));
  const auto freeDescriptor = qScopeGuard([&]() { LocalFree(descriptor); });
  PACL acl = nullptr;
  BOOL present = FALSE;
  BOOL defaulted = FALSE;
  QVERIFY(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted));
  QVERIFY(present);
  auto native = QDir::toNativeSeparators(path);
  QCOMPARE(SetNamedSecurityInfoW(reinterpret_cast<LPWSTR>(native.data()), SE_FILE_OBJECT,
                                 DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                 nullptr, nullptr, acl, nullptr),
           DWORD(ERROR_SUCCESS));
#else
  QCOMPARE(chmod(QFile::encodeName(path).constData(), 0644), 0);
#endif
  QVERIFY(!store.load(&records, &error));
  QCOMPARE(error, QStringLiteral("credential_store_permissions"));
}

void TestHttpApiLifecycle::controllerOwnsStartupDisableAndRevocation() {
  // vxcore test mode uses a fixed child name under the OS temp directory. Give
  // this process a unique parent so parallel test executables cannot share it.
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QList<QByteArray> keys{QByteArray("TMP"), QByteArray("TEMP"), QByteArray("TMPDIR")};
  QMap<QByteArray, QByteArray> previous;
  QSet<QByteArray> present;
  for (const auto &key : keys) {
    if (qEnvironmentVariableIsSet(key.constData())) {
      present.insert(key);
    }
    previous.insert(key, qgetenv(key.constData()));
    qputenv(key.constData(), QFile::encodeName(temporary.path()));
  }
  const auto restoreEnvironment = qScopeGuard([&]() {
    for (const auto &key : keys) {
      if (present.contains(key)) {
        qputenv(key.constData(), previous.value(key));
      } else {
        qunsetenv(key.constData());
      }
    }
  });
  vxcore_set_test_mode(1);
  VxCoreContextHandle context = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &context), VXCORE_OK);
  QVERIFY(context);
  const auto destroyContext = qScopeGuard([&]() { vxcore_context_destroy(context); });
  ServiceLocator services;
  ConfigCoreService configCore(context);
  ConfigMgr2 config(&configCore);
  config.init();
  HookManager hooks;
  NotebookIoGate gate;
  SyncWorkQueueManager queues;
  NotebookCoreService notebooks(context);
  notebooks.setHookManager(&hooks);
  notebooks.setNotebookIoGate(&gate);
  BufferService buffers(context, &hooks, &gate);
  buffers.setSyncWorkQueueManager(&queues);
  CommentService comments(&notebooks, &gate, &hooks);
  NotificationService notifications;
  SearchCoreService searchCore(context);
  SearchService search(&searchCore);
  services.registerService<ConfigMgr2>(&config);
  services.registerService<NotebookCoreService>(&notebooks);
  services.registerService<BufferService>(&buffers);
  services.registerService<NotebookIoGate>(&gate);
  services.registerService<HookManager>(&hooks);
  services.registerService<SyncWorkQueueManager>(&queues);
  services.registerService<CommentService>(&comments);
  services.registerService<NotificationService>(&notifications);
  services.registerService<SearchService>(&search);
  services.registerService<SearchCoreService>(&searchCore);
  const auto notebook =
      notebooks.createNotebook(temporary.filePath(QStringLiteral("notebook")),
                               QStringLiteral("{\"name\":\"API fixture\"}"), NotebookType::Bundled);
  QVERIFY(!notebook.isEmpty());
  const auto port = availablePort();
  QVERIFY(port != 0);
  auto &settings = config.getSessionConfig().getHttpApiConfig();
  settings.setAddress(QStringLiteral("127.0.0.1"));
  settings.setPort(port);
  settings.setEnabled(false);
  QString token;
  QString tokenId;
  QString error;
#ifdef Q_OS_WIN
  const auto registryDirectory =
      QDir(config.getUserConfigPath()).filePath(QStringLiteral("http-api"));
#else
  const auto registryDirectory = config.getFileFromConfigFolder(QStringLiteral("http-api"));
#endif
  const auto registry = QDir(registryDirectory).filePath(QStringLiteral("tokens.json"));
  {
    HttpApiController controller(services);
    QVERIFY(!controller.isListening());
    QVERIFY(!QFileInfo::exists(registryDirectory));
    QVERIFY2(controller.issueToken(QStringLiteral("Agent"), {notebook},
                                   {QStringLiteral("notes:read")}, std::nullopt, &token, &error),
             qPrintable(error));
    QVERIFY(!controller.isListening());
    {
      ApiCredentialStore persistedStore(registryDirectory);
      ApiAuth persistedAuth(persistedStore);
      QVERIFY2(persistedAuth.load(&error), qPrintable(error));
      QVERIFY(persistedAuth.authenticate(token.toUtf8()).has_value());
    }
#ifdef Q_OS_WIN
    QVERIFY(!QFileInfo::exists(config.getFileFromConfigFolder(QStringLiteral("http-api"))));
#endif
    tokenId = token.section(QLatin1Char('.'), 0, 0);
    settings.setEnabled(true);
    QVERIFY2(controller.apply(&error), qPrintable(error));
    QVERIFY(!controller.isListening());
    controller.applicationReady();
    QVERIFY2(controller.isListening(), qPrintable(controller.statusText()));
    QCOMPARE(request(controller.endpoint()).status, 401);
    QCOMPARE(request(controller.endpoint(), token).status, 200);

    QString rejectedSecret;
    QVERIFY(!controller.issueToken(QStringLiteral("Invalid client"), {notebook}, {},
                                   QDateTime::currentDateTimeUtc().addDays(1), &rejectedSecret,
                                   &error));
    QVERIFY(rejectedSecret.isEmpty());
    QCOMPARE(request(controller.endpoint(), token).status, 200);

    const auto backup = registry + QStringLiteral(".saved");
    QVERIFY(QFile::rename(registry, backup));
    {
      const auto restoreRegistry = qScopeGuard([&]() {
        QDir().rmdir(registry);
        QFile::rename(backup, registry);
      });
      QVERIFY(QDir().mkdir(registry));
      QVERIFY(!controller.issueToken(
          QStringLiteral("Storage failure"), {notebook}, {QStringLiteral("notes:read")},
          QDateTime::currentDateTimeUtc().addDays(1), &rejectedSecret, &error));
      QVERIFY(rejectedSecret.isEmpty());
      QCOMPARE(request(controller.endpoint(), token).status, 0);
    }
    QVERIFY2(controller.apply(&error), qPrintable(error));
    QCOMPARE(request(controller.endpoint(), token).status, 200);

    settings.setEnabled(false);
    QVERIFY2(controller.apply(&error), qPrintable(error));
    QVERIFY(!controller.isListening());
    QCOMPARE(request(controller.endpoint(), token).status, 0);
    settings.setEnabled(true);
    QVERIFY2(controller.apply(&error), qPrintable(error));
    QCOMPARE(request(controller.endpoint(), token).status, 200);

    settings.setAddress(QStringLiteral("0.0.0.0"));
    QVERIFY(!controller.apply(&error));
    QVERIFY(!controller.isListening());
    settings.setAddress(QStringLiteral("127.0.0.1"));
    QVERIFY2(controller.apply(&error), qPrintable(error));

    QTcpSocket partial;
    partial.connectToHost(QHostAddress::LocalHost, port);
    QVERIFY(partial.waitForConnected(1000));
    partial.write("GET /api/v1/info HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer ");
    if (partial.bytesToWrite() > 0) {
      QVERIFY(partial.waitForBytesWritten(1000));
    }
    QElapsedTimer stopTimer;
    stopTimer.start();
    controller.shutdown();
    QVERIFY2(stopTimer.elapsed() < 3000, "HTTP shutdown waited for an unfinished socket request");
    QVERIFY(!controller.isListening());
  }
  {
    HttpApiController controller(services);
    controller.applicationReady();
    QVERIFY2(controller.isListening(), qPrintable(controller.statusText()));
    QCOMPARE(request(controller.endpoint(), token).status, 200);
    QVERIFY2(controller.revokeToken(tokenId, &error), qPrintable(error));
    QCOMPARE(request(controller.endpoint(), token).status, 401);
    controller.shutdown();
  }
  {
    HttpApiController controller(services);
    controller.applicationReady();
    QVERIFY2(controller.isListening(), qPrintable(controller.statusText()));
    QCOMPARE(request(controller.endpoint(), token).status, 401);
    QVERIFY(notifications.messages().isEmpty());
    QString failureToken;
    QVERIFY2(controller.issueToken(QStringLiteral("Notification failure"), {notebook},
                                   {QStringLiteral("notes:read")}, std::nullopt, &failureToken,
                                   &error),
             qPrintable(error));
    QVERIFY(notifications.messages().isEmpty());
    const auto backup = registry + QStringLiteral(".saved");
    QVERIFY(QFile::rename(registry, backup));
    const auto restoreRegistry = qScopeGuard([&]() {
      QDir().rmdir(registry);
      QFile::rename(backup, registry);
    });
    QVERIFY(QDir().mkdir(registry));
    QVERIFY(!controller.revokeToken(failureToken.section(QLatin1Char('.'), 0, 0), &error));
    QVERIFY(!controller.isListening());
    QCOMPARE(notifications.activeCount(), 1);
    const auto &message = notifications.messages().constFirst();
    QCOMPARE(message.m_severity, NotificationMessage::Severity::Error);
    QCOMPARE(message.m_attention, NotificationMessage::Attention::Interrupt);
    QCOMPARE(message.m_duration, NotificationMessage::Duration::Persist);
    QCOMPARE(message.m_category, QStringLiteral("httpapi"));
    QVERIFY(!message.m_text.contains(failureToken));
    controller.shutdown();
  }
}

QTEST_GUILESS_MAIN(TestHttpApiLifecycle)
#include "test_httpapilifecycle.moc"
