#include <QtTest>

#include <utils/urlutils.h>

using namespace vnotex;

namespace tests {

class TestUrlUtils : public QObject {
  Q_OBJECT

private slots:
  void testPathWithFragment();
  void testPathWithoutFragment();
  void testPathWithEmptyFragment();
  void testMultipleHashes();
  void testUrlEncodedFragment();
  void testAnchorOnlyLink();
  void testAbsolutePathWithFragment();
  void testEmptyString();
};

void TestUrlUtils::testPathWithFragment() {
  auto result = splitUrlFragment(QStringLiteral("other.md#heading"));
  QCOMPARE(result.path, QStringLiteral("other.md"));
  QCOMPARE(result.fragment, QStringLiteral("heading"));
}

void TestUrlUtils::testPathWithoutFragment() {
  auto result = splitUrlFragment(QStringLiteral("other.md"));
  QCOMPARE(result.path, QStringLiteral("other.md"));
  QCOMPARE(result.fragment, QString());
}

void TestUrlUtils::testPathWithEmptyFragment() {
  auto result = splitUrlFragment(QStringLiteral("other.md#"));
  QCOMPARE(result.path, QStringLiteral("other.md"));
  QCOMPARE(result.fragment, QString());
}

void TestUrlUtils::testMultipleHashes() {
  auto result = splitUrlFragment(QStringLiteral("other.md#a#b"));
  QCOMPARE(result.path, QStringLiteral("other.md"));
  QCOMPARE(result.fragment, QStringLiteral("a#b"));
}

void TestUrlUtils::testUrlEncodedFragment() {
  auto result = splitUrlFragment(QStringLiteral("other.md#caf%C3%A9"));
  QCOMPARE(result.path, QStringLiteral("other.md"));
  QCOMPARE(result.fragment, QStringLiteral("café"));
}

void TestUrlUtils::testAnchorOnlyLink() {
  auto result = splitUrlFragment(QStringLiteral("#heading"));
  QCOMPARE(result.path, QString());
  QCOMPARE(result.fragment, QStringLiteral("heading"));
}

void TestUrlUtils::testAbsolutePathWithFragment() {
  auto result = splitUrlFragment(QStringLiteral("/abs/path/file.md#heading"));
  QCOMPARE(result.path, QStringLiteral("/abs/path/file.md"));
  QCOMPARE(result.fragment, QStringLiteral("heading"));
}

void TestUrlUtils::testEmptyString() {
  auto result = splitUrlFragment(QString());
  QCOMPARE(result.path, QString());
  QCOMPARE(result.fragment, QString());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestUrlUtils)
#include "test_urlutils.moc"
