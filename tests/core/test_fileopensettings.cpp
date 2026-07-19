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
  void testCursorOffsetRoundTrip();
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

void TestFileOpenSettings::testCursorOffsetRoundTrip() {
  // Default is -1 and round-trips.
  {
    FileOpenSettings settings;
    QCOMPARE(settings.m_cursorOffset, -1);
    FileOpenSettings restored = FileOpenSettings::fromVariantMap(settings.toVariantMap());
    QCOMPARE(restored.m_cursorOffset, -1);
  }

  // A concrete offset round-trips through the variant map.
  {
    FileOpenSettings settings;
    settings.m_cursorOffset = 123;
    QVariantMap map = settings.toVariantMap();
    QCOMPARE(map.value(QStringLiteral("cursorOffset")).toInt(), 123);
    FileOpenSettings restored = FileOpenSettings::fromVariantMap(map);
    QCOMPARE(restored.m_cursorOffset, 123);
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFileOpenSettings)
#include "test_fileopensettings.moc"
