#include <QJsonDocument>
#include <QThread>
#include <QtTest>
#include <api/apiauth.h>
#include <future>

using namespace vnotex;

namespace {
class MemoryStore : public IApiCredentialStore {
public:
  QJsonArray records;
  bool failLoad = false;
  bool failSave = false;
  int saves = 0;
  QThread *owner = QThread::currentThread();
  bool load(QJsonArray *p_records, QString *p_error) override {
    Q_ASSERT(QThread::currentThread() == owner);
    if (failLoad) {
      *p_error = QStringLiteral("injected_load_failure");
      return false;
    }
    *p_records = records;
    return true;
  }
  bool save(const QJsonArray &p_records, QString *p_error) override {
    Q_ASSERT(QThread::currentThread() == owner);
    ++saves;
    if (failSave) {
      *p_error = QStringLiteral("injected_save_failure");
      return false;
    }
    records = p_records;
    return true;
  }
};

ApiTokenGrant grant() {
  return {QStringLiteral("automation"),
          {QStringLiteral("nb-a")},
          {QStringLiteral("notes:read")},
          QDateTime::currentDateTimeUtc().addDays(90)};
}
} // namespace

class TestApiAuth : public QObject {
  Q_OBJECT
private slots:
  void durableIssueAndReload() {
    MemoryStore store;
    ApiAuth auth(store);
    ApiIssuedToken token;
    QString error;
    QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Issued);
    QCOMPARE(token.id.size(), 32);
    QCOMPARE(token.secret.size(), 76);
    QVERIFY(token.secret.startsWith(token.id + '.'));
    const auto identity = auth.authenticate(token.secret.toLatin1());
    QVERIFY(identity);
    QCOMPARE(identity->tokenId, token.id);
    QVERIFY(!auth.authenticate(QByteArray()));
    QVERIFY(!auth.authenticate(token.secret.toLatin1() + 'x'));
    auto altered = token.secret.toLatin1();
    altered[40] = altered[40] == 'a' ? 'b' : 'a';
    QVERIFY(!auth.authenticate(altered));
    const auto disk = QJsonDocument(store.records).toJson();
    QVERIFY(!disk.contains(token.secret.toLatin1()));
    QVERIFY(!disk.contains(token.secret.sliced(33).toLatin1()));
    QVERIFY(store.records[0].toObject().contains(QStringLiteral("verifier")));
    const auto publicJson = QJsonDocument(auth.tokens()).toJson();
    QVERIFY(!publicJson.contains("verifier"));
    QVERIFY(!publicJson.contains("secret"));
    ApiAuth reloaded(store);
    QVERIFY(reloaded.load(&error));
    QVERIFY(reloaded.authenticate(token.secret.toLatin1()));
    QCOMPARE(store.saves, 1);
  }

  void failedPublicationAndRevocation() {
    MemoryStore store;
    ApiAuth auth(store);
    ApiIssuedToken token;
    QString error;
    store.failSave = true;
    QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Unavailable);
    QVERIFY(token.secret.isEmpty());
    QVERIFY(auth.tokens().isEmpty());
    store.failSave = false;
    QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Issued);
    const auto identity = auth.authenticate(token.secret.toLatin1());
    QVERIFY(identity);
    store.failSave = true;
    QVERIFY(!auth.revoke(token.id, &error));
    QVERIFY(!auth.authenticate(token.secret.toLatin1()));
    QVERIFY(!auth.isAuthorized(*identity, ApiCommand{}));
    QVERIFY(auth.tokens().isEmpty());
    // The durable copy still contains the grant. The integration must stop on failed revoke.
    QCOMPARE(store.records.size(), 1);
  }

  void scopesAndNotebookIsolation() {
    MemoryStore store;
    ApiAuth auth(store);
    ApiIssuedToken token;
    QString error;
    QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Issued);
    const auto identity = *auth.authenticate(token.secret.toLatin1());
    ApiCommand command;
    command.kind = ApiCommandKind::ReadContent;
    command.notebookId = QStringLiteral("nb-a");
    QVERIFY(auth.isAuthorized(identity, command));
    command.notebookId = QStringLiteral("nb-b");
    QVERIFY(!auth.isAuthorized(identity, command));
    command.notebookId = QStringLiteral("nb-a");
    command.view = ApiContentView::Live;
    QVERIFY(!auth.isAuthorized(identity, command));
    command.kind = ApiCommandKind::ReplaceContent;
    QVERIFY(!auth.isAuthorized(identity, command));
    command.kind = ApiCommandKind::TrashNote;
    QVERIFY(!auth.isAuthorized(identity, command));
    command.kind = ApiCommandKind::Search;
    command.notebookIds = {QStringLiteral("nb-b")};
    QVERIFY(!auth.isAuthorized(identity, command));
  }

  void expiredDisabledAndCorruptRegistry() {
    MemoryStore store;
    ApiAuth auth(store);
    ApiIssuedToken token;
    QString error;
    QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Issued);
    const auto original = store.records;
    auto record = store.records[0].toObject();
    record.insert(QStringLiteral("issuedAt"),
                  QDateTime::currentDateTimeUtc().addDays(-2).toString(Qt::ISODateWithMs));
    record.insert(QStringLiteral("expiresAt"),
                  QDateTime::currentDateTimeUtc().addDays(-1).toString(Qt::ISODateWithMs));
    store.records = {record};
    QVERIFY(auth.load(&error));
    QVERIFY(!auth.authenticate(token.secret.toLatin1()));
    record = original[0].toObject();
    record.insert(QStringLiteral("enabled"), false);
    store.records = {record};
    QVERIFY(auth.load(&error));
    QVERIFY(!auth.authenticate(token.secret.toLatin1()));
    record.insert(QStringLiteral("verifier"), QStringLiteral("not-a-digest"));
    store.records = {record};
    QVERIFY(!auth.load(&error));
    QVERIFY(auth.tokens().isEmpty());
    store.records = original;
    store.records.append(original[0]);
    QVERIFY(!auth.load(&error));
    store.records = original;
    record = original[0].toObject();
    record.insert(QStringLiteral("enabled"), QStringLiteral("true"));
    store.records = {record};
    QVERIFY(!auth.load(&error));
    store.failLoad = true;
    QVERIFY(!auth.load(&error));
    QVERIFY(!auth.authenticate(token.secret.toLatin1()));
  }

  void neverTokensRemainScopedAndRevocable() {
    MemoryStore store;
    ApiAuth auth(store);
    ApiIssuedToken token;
    QString error;
    auto unlimited = grant();
    unlimited.expiresAt = std::nullopt;
    QCOMPARE(auth.issue(unlimited, &token, &error), ApiAuth::IssueResult::Issued);
    auto record = store.records[0].toObject();
    QVERIFY(record.value("expiresAt").isNull());
    record.insert("issuedAt",
                  QDateTime::currentDateTimeUtc().addYears(-5).toString(Qt::ISODateWithMs));
    store.records = {record};
    ApiAuth reloaded(store);
    QVERIFY(reloaded.load(&error));
    const auto principal = reloaded.authenticate(token.secret.toUtf8());
    QVERIFY(principal.has_value());
    ApiCommand command;
    command.kind = ApiCommandKind::ReadContent;
    command.notebookId = QStringLiteral("nb-a");
    QVERIFY(reloaded.isAuthorized(*principal, command));
    command.notebookId = QStringLiteral("nb-b");
    QVERIFY(!reloaded.isAuthorized(*principal, command));
    command.notebookId = QStringLiteral("nb-a");
    command.kind = ApiCommandKind::ReplaceContent;
    QVERIFY(!reloaded.isAuthorized(*principal, command));
    record.insert("enabled", false);
    store.records = {record};
    QVERIFY(reloaded.load(&error));
    QVERIFY(!reloaded.authenticate(token.secret.toUtf8()).has_value());
    command.kind = ApiCommandKind::ReadContent;
    QVERIFY(!reloaded.isAuthorized(*principal, command));
    record.insert("enabled", true);
    store.records = {record};
    QVERIFY(reloaded.load(&error));
    QVERIFY(reloaded.revoke(token.id, &error));
    QVERIFY(!reloaded.authenticate(token.secret.toUtf8()).has_value());
    QVERIFY(!reloaded.isAuthorized(*principal, command));
    ApiAuth revoked(store);
    QVERIFY(revoked.load(&error));
    QVERIFY(!revoked.authenticate(token.secret.toUtf8()).has_value());
  }

  void malformedExpiryNeverGrantsAuthority_data() {
    QTest::addColumn<QJsonValue>("expiry");
    QTest::newRow("missing") << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("numeric-zero") << QJsonValue(0);
    QTest::newRow("invalid-string") << QJsonValue(QStringLiteral("Never"));
    QTest::newRow("before-issuance")
        << QJsonValue(QDateTime::currentDateTimeUtc().addDays(-1).toString(Qt::ISODateWithMs));
  }

  void malformedExpiryNeverGrantsAuthority() {
    QFETCH(QJsonValue, expiry);
    MemoryStore store;
    ApiAuth auth(store);
    ApiIssuedToken token;
    QString error;
    auto unlimited = grant();
    unlimited.expiresAt = std::nullopt;
    QCOMPARE(auth.issue(unlimited, &token, &error), ApiAuth::IssueResult::Issued);
    auto record = store.records[0].toObject();
    if (expiry.isUndefined())
      record.remove("expiresAt");
    else
      record.insert("expiresAt", expiry);
    store.records = {record};
    QVERIFY(!auth.load(&error));
    QVERIFY(!auth.authenticate(token.secret.toUtf8()).has_value());
    QVERIFY(auth.tokens().isEmpty());
  }

  void unspecifiedGrantExpiryIsRejected() {
    MemoryStore store;
    ApiAuth auth(store);
    ApiTokenGrant unspecified{
        QStringLiteral("automation"), {QStringLiteral("nb-a")}, {QStringLiteral("notes:read")}};
    ApiIssuedToken token;
    QString error;
    QCOMPARE(auth.issue(unspecified, &token, &error), ApiAuth::IssueResult::Rejected);
    QVERIFY(token.secret.isEmpty());
    QVERIFY(store.records.isEmpty());
  }

  void neverTokensCountTowardQuotaAndDisabledArePruned() {
    MemoryStore store;
    ApiAuth auth(store);
    auto unlimited = grant();
    unlimited.expiresAt = std::nullopt;
    ApiIssuedToken token;
    QString error, firstBearer, secondId, lastId;
    for (int i = 0; i < 64; ++i) {
      QCOMPARE(auth.issue(unlimited, &token, &error), ApiAuth::IssueResult::Issued);
      if (i == 0)
        firstBearer = token.secret;
      if (i == 1)
        secondId = token.id;
      lastId = token.id;
    }
    QCOMPARE(auth.issue(unlimited, &token, &error), ApiAuth::IssueResult::Rejected);
    for (int i = 0; i < store.records.size(); ++i) {
      auto record = store.records[i].toObject();
      if (record.value("id").toString() == lastId) {
        record.insert("enabled", false);
        store.records[i] = record;
      }
    }
    QVERIFY(auth.load(&error));
    QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Issued);
    QCOMPARE(auth.tokens().size(), 64);
    for (const auto &record : auth.tokens())
      QVERIFY(record.toObject().value("id").toString() != lastId);
    QVERIFY(auth.authenticate(firstBearer.toUtf8()).has_value());
    QVERIFY(auth.revoke(secondId, &error));
    QCOMPARE(auth.issue(unlimited, &token, &error), ApiAuth::IssueResult::Issued);
    QCOMPARE(auth.tokens().size(), 64);
    QVERIFY(auth.authenticate(firstBearer.toUtf8()).has_value());
  }

  void finiteRegistryAndGrantValidation() {
    MemoryStore store;
    ApiAuth auth(store);
    QString error;
    QSet<QString> secrets;
    ApiIssuedToken token;
    auto invalidGrant = grant();
    invalidGrant.notebookIds.clear();
    QCOMPARE(auth.issue(invalidGrant, &token, &error), ApiAuth::IssueResult::Rejected);
    invalidGrant = grant();
    invalidGrant.scopes.insert(QStringLiteral("admin"));
    QCOMPARE(auth.issue(invalidGrant, &token, &error), ApiAuth::IssueResult::Rejected);
    invalidGrant = grant();
    invalidGrant.expiresAt = QDateTime::currentDateTimeUtc().addSecs(-1);
    QCOMPARE(auth.issue(invalidGrant, &token, &error), ApiAuth::IssueResult::Rejected);
    for (int i = 0; i < 64; ++i) {
      QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Issued);
      QVERIFY(!secrets.contains(token.secret));
      secrets.insert(token.secret);
    }
    const auto last = token.id;
    QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Rejected);
    QCOMPARE(auth.tokens().size(), 64);
    QVERIFY(auth.revoke(last, &error));
    QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Issued);
    QCOMPARE(auth.tokens().size(), 64);
  }

  void adminThreadAndConcurrentReaders() {
    MemoryStore store;
    ApiAuth auth(store);
    ApiIssuedToken token;
    QString error;
    QCOMPARE(auth.issue(grant(), &token, &error), ApiAuth::IssueResult::Issued);
    const auto identity = *auth.authenticate(token.secret.toLatin1());
    auto worker = std::async(std::launch::async, [&] {
      QString workerError;
      ApiIssuedToken rejected;
      if (auth.issue(grant(), &rejected, &workerError) != ApiAuth::IssueResult::Unavailable) {
        return false;
      }
      return auth.authenticate(token.secret.toLatin1()).has_value() &&
             auth.isAuthorized(identity, ApiCommand{});
    });
    QVERIFY(worker.get());
    std::atomic_bool stop{false};
    auto reader = std::async(std::launch::async, [&] {
      while (!stop.load()) {
        auth.authenticate(token.secret.toLatin1());
        auth.isAuthorized(identity, ApiCommand{});
      }
    });
    QVERIFY(auth.revoke(token.id, &error));
    stop.store(true);
    reader.get();
    QVERIFY(!auth.isAuthorized(identity, ApiCommand{}));
    QCOMPARE(store.saves, 2);
  }
};

QTEST_GUILESS_MAIN(TestApiAuth)
#include "test_apiauth.moc"
