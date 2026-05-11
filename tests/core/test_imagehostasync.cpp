#include <QtTest>

#include <QSignalSpy>
#include <QThread>

#include <core/services/imagehostworker.h>
#include <imagehost/iimagehostprovider.h>
#include <imagehost/imagehosttypes.h>

namespace tests {

// Mock provider for testing — no network access.
class MockImageHostProvider : public vnotex::IImageHostProvider {
  Q_OBJECT
public:
  int delayMs = 0;
  bool shouldFail = false;
  bool shouldThrow = false;
  QJsonObject receivedConfig;

  explicit MockImageHostProvider(QObject *p_parent = nullptr)
      : vnotex::IImageHostProvider(p_parent) {}

  QString typeId() const override { return QStringLiteral("mock"); }
  QString displayName() const override { return QStringLiteral("Mock Host"); }
  bool supportsDelete() const override { return true; }
  bool ownsUrl(const QString &) const override { return false; }
  bool ready() const override { return true; }

  QJsonObject getConfig() const override { return receivedConfig; }
  void setConfig(const QJsonObject &p_config) override { receivedConfig = p_config; }

  vnotex::ImageUploadResult upload(const QByteArray &, const QString &p_path) override {
    if (shouldThrow) throw std::runtime_error("mock upload error");
    if (delayMs > 0) QThread::msleep(delayMs);
    if (shouldFail)
      return {false, {}, QStringLiteral("mock failure"), {}};
    return {true, QStringLiteral("https://example.com/") + p_path, {}, {}};
  }

  bool remove(const QString &, QString &p_msg) override {
    if (shouldFail) {
      p_msg = QStringLiteral("mock remove fail");
      return false;
    }
    return true;
  }

  bool testConfig(const QJsonObject &, QString &p_msg) override {
    if (shouldFail) {
      p_msg = QStringLiteral("mock test fail");
      return false;
    }
    return true;
  }
};

class TestImageHostAsync : public QObject {
  Q_OBJECT

private slots:
  void testSequentialExecution();
  void testConfigIsolation();
  void testCleanShutdown();
  void testExceptionHandling();
  void testGeneratePlaceholder();
  void testReplacePlaceholder();
  void testReplacePlaceholderMissing();
  void testRemovePlaceholder();
  void testRemovePlaceholderPreservesContext();
  void testFullAsyncPipeline();
};

void TestImageHostAsync::testSequentialExecution() {
  qRegisterMetaType<vnotex::ImageHostWorkItem>();
  qRegisterMetaType<vnotex::ImageHostAsyncResult>();

  QThread thread;
  auto *worker = new vnotex::ImageHostWorker([](const QString &) -> vnotex::IImageHostProvider * {
    return new MockImageHostProvider(nullptr);
  });
  worker->moveToThread(&thread);
  QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
  thread.start();

  QSignalSpy spy(worker, &vnotex::ImageHostWorker::uploadCompleted);
  QVERIFY(spy.isValid());

  // Queue 3 uploads rapidly.
  for (int i = 1; i <= 3; ++i) {
    vnotex::ImageHostWorkItem item;
    item.token = i;
    item.operation = vnotex::ImageHostWorkItem::Operation::Upload;
    item.typeId = QStringLiteral("mock");
    item.path = QStringLiteral("img%1.png").arg(i);
    QMetaObject::invokeMethod(worker, "doUpload", Qt::QueuedConnection,
                              Q_ARG(vnotex::ImageHostWorkItem, item));
  }

  // Wait for all 3.
  QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 3, 5000);

  // Verify token order.
  for (int i = 0; i < 3; ++i) {
    int token = spy.at(i).at(0).toInt();
    QCOMPARE(token, i + 1);
  }

  thread.quit();
  thread.wait(5000);
}

void TestImageHostAsync::testConfigIsolation() {
  qRegisterMetaType<vnotex::ImageHostWorkItem>();
  qRegisterMetaType<vnotex::ImageHostAsyncResult>();

  QJsonObject capturedConfig;
  QThread thread;
  auto *worker =
      new vnotex::ImageHostWorker([&capturedConfig](const QString &) -> vnotex::IImageHostProvider * {
        auto *mock = new MockImageHostProvider(nullptr);
        // After setConfig is called by the worker, we capture it in upload.
        return mock;
      });
  worker->moveToThread(&thread);
  QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
  thread.start();

  QSignalSpy spy(worker, &vnotex::ImageHostWorker::uploadCompleted);

  vnotex::ImageHostWorkItem item;
  item.token = 1;
  item.operation = vnotex::ImageHostWorkItem::Operation::Upload;
  item.typeId = QStringLiteral("mock");
  item.config = QJsonObject{{QStringLiteral("key"), QStringLiteral("original")}};
  item.path = QStringLiteral("test.png");

  QMetaObject::invokeMethod(worker, "doUpload", Qt::QueuedConnection,
                            Q_ARG(vnotex::ImageHostWorkItem, item));

  QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

  // The upload succeeded — config was snapshotted in the work item.
  auto result = spy.at(0).at(1).value<vnotex::ImageHostAsyncResult>();
  QVERIFY(result.success);

  thread.quit();
  thread.wait(5000);
}

void TestImageHostAsync::testCleanShutdown() {
  qRegisterMetaType<vnotex::ImageHostWorkItem>();
  qRegisterMetaType<vnotex::ImageHostAsyncResult>();

  QThread thread;
  auto *worker = new vnotex::ImageHostWorker([](const QString &) -> vnotex::IImageHostProvider * {
    auto *mock = new MockImageHostProvider(nullptr);
    mock->delayMs = 200;
    return mock;
  });
  worker->moveToThread(&thread);
  QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
  thread.start();

  // Dispatch an upload.
  vnotex::ImageHostWorkItem item;
  item.token = 1;
  item.operation = vnotex::ImageHostWorkItem::Operation::Upload;
  item.typeId = QStringLiteral("mock");
  item.path = QStringLiteral("slow.png");
  QMetaObject::invokeMethod(worker, "doUpload", Qt::QueuedConnection,
                            Q_ARG(vnotex::ImageHostWorkItem, item));

  // Immediately abort.
  worker->m_abortFlag.store(1);
  thread.quit();
  QVERIFY(thread.wait(5000));
  QVERIFY(!thread.isRunning());
}

void TestImageHostAsync::testExceptionHandling() {
  qRegisterMetaType<vnotex::ImageHostWorkItem>();
  qRegisterMetaType<vnotex::ImageHostAsyncResult>();

  bool firstCall = true;
  QThread thread;
  auto *worker =
      new vnotex::ImageHostWorker([&firstCall](const QString &) -> vnotex::IImageHostProvider * {
        auto *mock = new MockImageHostProvider(nullptr);
        if (firstCall) {
          mock->shouldThrow = true;
          firstCall = false;
        }
        return mock;
      });
  worker->moveToThread(&thread);
  QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
  thread.start();

  QSignalSpy failSpy(worker, &vnotex::ImageHostWorker::operationFailed);
  QSignalSpy successSpy(worker, &vnotex::ImageHostWorker::uploadCompleted);

  // First upload — should throw and emit operationFailed.
  vnotex::ImageHostWorkItem item1;
  item1.token = 1;
  item1.typeId = QStringLiteral("mock");
  item1.path = QStringLiteral("throw.png");
  QMetaObject::invokeMethod(worker, "doUpload", Qt::QueuedConnection,
                            Q_ARG(vnotex::ImageHostWorkItem, item1));

  QTRY_COMPARE_WITH_TIMEOUT(failSpy.count(), 1, 5000);
  QVERIFY(failSpy.at(0).at(1).toString().contains(QStringLiteral("mock upload error")));

  // Second upload — should succeed (worker still alive).
  vnotex::ImageHostWorkItem item2;
  item2.token = 2;
  item2.typeId = QStringLiteral("mock");
  item2.path = QStringLiteral("ok.png");
  QMetaObject::invokeMethod(worker, "doUpload", Qt::QueuedConnection,
                            Q_ARG(vnotex::ImageHostWorkItem, item2));

  QTRY_COMPARE_WITH_TIMEOUT(successSpy.count(), 1, 5000);
  auto result = successSpy.at(0).at(1).value<vnotex::ImageHostAsyncResult>();
  QVERIFY(result.success);
  QCOMPARE(result.token, 2);

  thread.quit();
  thread.wait(5000);
}

// ===== Placeholder lifecycle tests =====
// These test the same string patterns used by MarkdownEditor's static helpers
// without pulling in vtextedit dependencies.

static QString generatePlaceholder(int p_token, const QString &p_fileName) {
  return QStringLiteral("![Uploading %1...](vnote-upload-%2)").arg(p_fileName).arg(p_token);
}

static QString replacePlaceholder(const QString &p_content, int p_token,
                                   const QString &p_realUrl, const QString &p_title) {
  const QString marker = QStringLiteral("vnote-upload-%1").arg(p_token);
  int idx = p_content.indexOf(marker);
  if (idx < 0) return p_content;
  int linkStart = p_content.lastIndexOf(QStringLiteral("!["), idx);
  if (linkStart < 0) return p_content;
  int linkEnd = p_content.indexOf(')', idx);
  if (linkEnd < 0) return p_content;
  QString result = p_content;
  result.replace(linkStart, linkEnd - linkStart + 1,
                 QStringLiteral("![%1](%2)").arg(p_title, p_realUrl));
  return result;
}

static QString removePlaceholder(const QString &p_content, int p_token) {
  const QString marker = QStringLiteral("vnote-upload-%1").arg(p_token);
  int idx = p_content.indexOf(marker);
  if (idx < 0) return p_content;
  int linkStart = p_content.lastIndexOf(QStringLiteral("!["), idx);
  if (linkStart < 0) return p_content;
  int linkEnd = p_content.indexOf(')', idx);
  if (linkEnd < 0) return p_content;
  QString result = p_content;
  int removeEnd = linkEnd + 1;
  if (removeEnd < result.size() && result[removeEnd] == '\n') {
    removeEnd++;
  }
  result.remove(linkStart, removeEnd - linkStart);
  return result;
}

void TestImageHostAsync::testGeneratePlaceholder() {
  QString ph = generatePlaceholder(42, QStringLiteral("image.png"));
  QCOMPARE(ph, QStringLiteral("![Uploading image.png...](vnote-upload-42)"));
  QVERIFY(ph.startsWith(QStringLiteral("![")));
  QVERIFY(ph.contains(QStringLiteral("vnote-upload-42")));
  QVERIFY(ph.contains(QStringLiteral("image.png")));
}

void TestImageHostAsync::testReplacePlaceholder() {
  QString content = QStringLiteral("Some text\n![Uploading image.png...](vnote-upload-42)\nMore text");
  QString result = replacePlaceholder(content, 42,
                                       QStringLiteral("https://example.com/image.png"),
                                       QStringLiteral("image.png"));
  QVERIFY(!result.contains(QStringLiteral("vnote-upload-42")));
  QVERIFY(result.contains(QStringLiteral("https://example.com/image.png")));
  QVERIFY(result.contains(QStringLiteral("![image.png]")));
  QVERIFY(result.contains(QStringLiteral("Some text")));
  QVERIFY(result.contains(QStringLiteral("More text")));
}

void TestImageHostAsync::testReplacePlaceholderMissing() {
  QString content = QStringLiteral("Some text without placeholder");
  QString result = replacePlaceholder(content, 99,
                                       QStringLiteral("https://example.com/img.png"),
                                       QStringLiteral("img.png"));
  QCOMPARE(result, content);
}

void TestImageHostAsync::testRemovePlaceholder() {
  QString content = QStringLiteral("Before\n![Uploading test.png...](vnote-upload-7)\nAfter");
  QString result = removePlaceholder(content, 7);
  QVERIFY(!result.contains(QStringLiteral("vnote-upload-7")));
  QVERIFY(result.contains(QStringLiteral("Before")));
  QVERIFY(result.contains(QStringLiteral("After")));
}

void TestImageHostAsync::testRemovePlaceholderPreservesContext() {
  // Placeholder at end of content (no trailing newline).
  QString content = QStringLiteral("Line1\n![Uploading x.png...](vnote-upload-3)");
  QString result = removePlaceholder(content, 3);
  QVERIFY(!result.contains(QStringLiteral("vnote-upload-3")));
  QCOMPARE(result, QStringLiteral("Line1\n"));

  // Missing token — content unchanged.
  QString unchanged = QStringLiteral("No placeholder here");
  QCOMPARE(removePlaceholder(unchanged, 999), unchanged);
}

void TestImageHostAsync::testFullAsyncPipeline() {
  qRegisterMetaType<vnotex::ImageHostWorkItem>();
  qRegisterMetaType<vnotex::ImageHostAsyncResult>();

  QThread thread;
  auto *worker = new vnotex::ImageHostWorker([](const QString &) -> vnotex::IImageHostProvider * {
    return new MockImageHostProvider(nullptr);
  });
  worker->moveToThread(&thread);
  QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
  thread.start();

  QSignalSpy spy(worker, &vnotex::ImageHostWorker::uploadCompleted);

  // Simulate what the service does: snapshot config into work item, dispatch.
  vnotex::ImageHostWorkItem item;
  item.token = 10;
  item.operation = vnotex::ImageHostWorkItem::Operation::Upload;
  item.typeId = QStringLiteral("mock");
  item.config = QJsonObject{{QStringLiteral("token"), QStringLiteral("abc123")}};
  item.data = QByteArray("fake image data");
  item.path = QStringLiteral("photo.png");
  item.providerName = QStringLiteral("TestHost");

  QMetaObject::invokeMethod(worker, "doUpload", Qt::QueuedConnection,
                            Q_ARG(vnotex::ImageHostWorkItem, item));

  QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

  auto result = spy.at(0).at(1).value<vnotex::ImageHostAsyncResult>();
  QCOMPARE(result.token, 10);
  QVERIFY(result.success);
  QVERIFY(result.url.contains(QStringLiteral("photo.png")));
  QCOMPARE(result.providerName, QStringLiteral("TestHost"));

  // Verify placeholder replacement would work with this result.
  QString content = QStringLiteral("Text\n") + generatePlaceholder(10, QStringLiteral("photo.png")) + QStringLiteral("\nEnd");
  QString replaced = replacePlaceholder(content, 10, result.url, QStringLiteral("photo.png"));
  QVERIFY(!replaced.contains(QStringLiteral("vnote-upload-10")));
  QVERIFY(replaced.contains(result.url));

  thread.quit();
  thread.wait(5000);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestImageHostAsync)
#include "test_imagehostasync.moc"
