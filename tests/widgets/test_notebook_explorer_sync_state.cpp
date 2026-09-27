// Exercises the production classifier consumed by NotebookExplorer2. Explorer
// visual behavior is not asserted by this service-only target.

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncstateclassifier.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestNotebookExplorerSyncState : public QObject {
  Q_OBJECT

private slots:
  void testClassifyS2WithRealClassifier();
};

void TestNotebookExplorerSyncState::testClassifyS2WithRealClassifier() {
  vxcore_set_test_mode(1);
  VxCoreContextHandle context = nullptr;
  QCOMPARE(vxcore_context_create("{}", &context), VXCORE_OK);
  {
    ServiceLocator services;
    NotebookCoreService notebooks(context);
    services.registerService<NotebookCoreService>(&notebooks);
    SyncCredentialsStore credentials(services);
    services.registerService<SyncCredentialsStore>(&credentials);
    SyncService sync(services);
    services.registerService<SyncService>(&sync);
    SyncStateClassifier classifier(services);
    TempDirFixture temp;
    QVERIFY(temp.isValid());
    const auto id = notebooks.createNotebook(temp.createDir("partial"),
                                             QStringLiteral(R"({"name":"Partial sync"})"),
                                             NotebookType::Bundled);
    QVERIFY(!id.isEmpty());
    QCOMPARE(classifier.classify(id), SyncState::S0);

    auto config = notebooks.getNotebookConfig(id);
    config[QStringLiteral("syncEnabled")] = true;
    config[QStringLiteral("syncBackend")] = QStringLiteral("webdav");
    config[QStringLiteral("syncRemoteUrl")] = QStringLiteral("https://example.com/dav/notebook/");
    QVERIFY(notebooks.updateNotebookConfig(
        id, QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact))));
    QCOMPARE(classifier.classify(id), SyncState::S2);
    QVERIFY(classifier.isPartial(classifier.classify(id)));

    config[QStringLiteral("syncBackend")] = QStringLiteral("unsupported");
    QVERIFY(notebooks.updateNotebookConfig(
        id, QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact))));
    QCOMPARE(classifier.classify(id), SyncState::S3);
    sync.shutdown();
    QVERIFY(notebooks.closeNotebook(id));
  }
  vxcore_context_destroy(context);
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookExplorerSyncState)
#include "test_notebook_explorer_sync_state.moc"
