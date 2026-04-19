// test_vxurl_parsing.cpp - Data-driven tests proving QUrl parses vx:// URLs correctly
#include <QtTest>

#include <QUrl>

namespace tests {

class TestVxUrlParsing : public QObject {
  Q_OBJECT

private slots:
  void testUrlParsing_data();
  void testUrlParsing();
};

void TestVxUrlParsing::testUrlParsing_data()
{
  QTest::addColumn<QString>("url");
  QTest::addColumn<QString>("scheme");
  QTest::addColumn<QString>("authority");
  QTest::addColumn<QStringList>("pathSegments");
  QTest::addColumn<QString>("fragment");

  QTest::newRow("full vx url")
      << "vx://settings/appearance/theme#quicknote"
      << "vx" << "settings"
      << QStringList{"appearance", "theme"}
      << "quicknote";

  QTest::newRow("authority only")
      << "vx://settings"
      << "vx" << "settings"
      << QStringList{}
      << "";

  QTest::newRow("authority with fragment")
      << "vx://settings#quicknote"
      << "vx" << "settings"
      << QStringList{}
      << "quicknote";

  QTest::newRow("deep path no fragment")
      << "vx://settings/editor/markdown"
      << "vx" << "settings"
      << QStringList{"editor", "markdown"}
      << "";

  QTest::newRow("single path segment")
      << "vx://settings/appearance"
      << "vx" << "settings"
      << QStringList{"appearance"}
      << "";

  QTest::newRow("extra slashes skipped")
      << "vx://settings///theme"
      << "vx" << "settings"
      << QStringList{"theme"}
      << "";

  QTest::newRow("different authority")
      << "vx://other/page"
      << "vx" << "other"
      << QStringList{"page"}
      << "";

  QTest::newRow("file url")
      << "file:///path/to/file.md"
      << "file" << ""
      << QStringList{"path", "to", "file.md"}
      << "";
}

void TestVxUrlParsing::testUrlParsing()
{
  QFETCH(QString, url);
  QFETCH(QString, scheme);
  QFETCH(QString, authority);
  QFETCH(QStringList, pathSegments);
  QFETCH(QString, fragment);

  QUrl parsed(url);
  QVERIFY(parsed.isValid());
  QCOMPARE(parsed.scheme(), scheme);
  QCOMPARE(parsed.authority(), authority);
  QCOMPARE(parsed.path().split("/", Qt::SkipEmptyParts), pathSegments);
  QCOMPARE(parsed.fragment(), fragment);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestVxUrlParsing)
#include "test_vxurl_parsing.moc"
