// Tests for LinkInsertUtils::appendFragmentToLink — issue #2656.
//
// "Insert As Relative Link" used to drop the heading anchor because
// QUrl::toLocalFile() strips the fragment before the relative link is built.
// The fragment is now re-appended via this pure helper. These tests pin the
// helper's contract in isolation (no editor / clipboard / dialog needed).

#include <QtTest>

#include <widgets/editors/linkinsertutils.h>

using namespace vnotex;

namespace tests {

class TestLinkInsertUtils : public QObject {
  Q_OBJECT

private slots:
  void testAppendFragment_data();
  void testAppendFragment();
};

void TestLinkInsertUtils::testAppendFragment_data() {
  QTest::addColumn<QString>("link");
  QTest::addColumn<QString>("fragment");
  QTest::addColumn<QString>("expected");

  QTest::newRow("empty fragment leaves link untouched")
      << "notes/FileName.md" << "" << "notes/FileName.md";
  QTest::newRow("simple anchor appended")
      << "FileName.md" << "My-Heading" << "FileName.md#My-Heading";
  QTest::newRow("relative path with anchor")
      << "../sub/FileName.md" << "Section-1" << "../sub/FileName.md#Section-1";
  QTest::newRow("already-encoded fragment preserved verbatim")
      << "File.md" << "My%20Heading" << "File.md#My%20Heading";
  QTest::newRow("empty link with fragment")
      << "" << "Anchor" << "#Anchor";
  QTest::newRow("link with spaces encoded, fragment kept")
      << "a%20b.md" << "H2" << "a%20b.md#H2";
}

void TestLinkInsertUtils::testAppendFragment() {
  QFETCH(QString, link);
  QFETCH(QString, fragment);
  QFETCH(QString, expected);

  QCOMPARE(LinkInsertUtils::appendFragmentToLink(link, fragment), expected);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestLinkInsertUtils)
#include "test_linkinsertutils.moc"
