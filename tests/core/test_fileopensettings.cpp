#include <QtTest>

#include <core/fileopensettings.h>

using namespace vnotex;

namespace tests {

class TestFileOpenSettings : public QObject {
  Q_OBJECT

private slots:
  void testAnchorRoundTrip();
  void testEmptyAnchorNotSerialized();
  void testExistingFieldsUnaffected();
  void testDefaultAnchorIsEmpty();
};

void TestFileOpenSettings::testAnchorRoundTrip() {
  FileOpenSettings settings;
  settings.m_anchor = QStringLiteral("test-heading");

  QVariantMap map = settings.toVariantMap();
  QVERIFY(map.contains(QStringLiteral("anchor")));
  QCOMPARE(map.value(QStringLiteral("anchor")).toString(), QStringLiteral("test-heading"));

  FileOpenSettings restored = FileOpenSettings::fromVariantMap(map);
  QCOMPARE(restored.m_anchor, QStringLiteral("test-heading"));
}

void TestFileOpenSettings::testEmptyAnchorNotSerialized() {
  FileOpenSettings settings;
  // m_anchor is default empty

  QVariantMap map = settings.toVariantMap();
  QVERIFY(!map.contains(QStringLiteral("anchor")));
}

void TestFileOpenSettings::testExistingFieldsUnaffected() {
  FileOpenSettings settings;
  settings.m_lineNumber = 42;
  settings.m_mode = ViewWindowMode::Edit;

  QVariantMap map = settings.toVariantMap();
  FileOpenSettings restored = FileOpenSettings::fromVariantMap(map);

  QCOMPARE(restored.m_lineNumber, 42);
  QCOMPARE(restored.m_mode, ViewWindowMode::Edit);
}

void TestFileOpenSettings::testDefaultAnchorIsEmpty() {
  FileOpenSettings settings;
  QVERIFY(settings.m_anchor.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFileOpenSettings)
#include "test_fileopensettings.moc"
