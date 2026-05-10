#include <QtTest>

#include <QJsonObject>

#include <imagehost/customcommandprovider.h>
#include <imagehost/imagehosttypes.h>

using namespace vnotex;

namespace tests {

class TestCustomCommandProvider : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testEmptyCommandNotReady();
  void testSupportsDeleteFalse();
  void testTypeIdCustomCommand();
  void testConfigRoundTrip();
  void testUploadWithEcho();

private:
  CustomCommandProvider *m_provider = nullptr;
};

void TestCustomCommandProvider::initTestCase() {
  m_provider = new CustomCommandProvider(this);
  QVERIFY(m_provider != nullptr);
}

void TestCustomCommandProvider::cleanupTestCase() {
  delete m_provider;
  m_provider = nullptr;
}

void TestCustomCommandProvider::testEmptyCommandNotReady() {
  CustomCommandProvider provider;
  QVERIFY(!provider.ready());
}

void TestCustomCommandProvider::testSupportsDeleteFalse() {
  CustomCommandProvider provider;
  QVERIFY(!provider.supportsDelete());
}

void TestCustomCommandProvider::testTypeIdCustomCommand() {
  CustomCommandProvider provider;
  QCOMPARE(provider.typeId(), QStringLiteral("custom_command"));
}

void TestCustomCommandProvider::testConfigRoundTrip() {
  CustomCommandProvider provider;

  QJsonObject config;
  config[QStringLiteral("command")] = QStringLiteral("echo test");
  provider.setConfig(config);

  QJsonObject retrieved = provider.getConfig();
  QCOMPARE(retrieved.value(QStringLiteral("command")).toString(), QStringLiteral("echo test"));

  // Verify provider is now ready.
  QVERIFY(provider.ready());
}

void TestCustomCommandProvider::testUploadWithEcho() {
  CustomCommandProvider provider;

  QJsonObject config;
#ifdef Q_OS_WIN
  config[QStringLiteral("command")] = QStringLiteral("cmd /c echo https://example.com/test.png");
#else
  config[QStringLiteral("command")] = QStringLiteral("echo https://example.com/test.png");
#endif
  provider.setConfig(config);

  QVERIFY(provider.ready());

  ImageUploadResult result = provider.upload(QByteArray("fake image data"), QStringLiteral("test.png"));
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QVERIFY(result.imageUrl.contains(QStringLiteral("https://example.com/test.png")));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestCustomCommandProvider)
#include "test_customcommandprovider.moc"
