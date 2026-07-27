// Tests for LinkInsertUtils — issue #2656 and same-file anchor collapsing.
//
// appendFragmentToLink: "Insert As Relative Link" used to drop the heading
// anchor because QUrl::toLocalFile() strips the fragment before the relative
// link is built. The fragment is now re-appended via this pure helper.
//
// composeRelativeLink: a relative link that points at a heading in the file
// being edited collapses to a bare "#anchor", since the file part would only be
// a self-reference.
//
// These tests pin the helpers' contracts in isolation (no editor / clipboard /
// dialog needed).

#include <QtTest>

#include <widgets/editors/linkinsertutils.h>

using namespace vnotex;

namespace tests {

class TestLinkInsertUtils : public QObject {
  Q_OBJECT

private slots:
  void testAppendFragment_data();
  void testAppendFragment();

  void testComposeRelativeLink_data();
  void testComposeRelativeLink();

  void testComposeMatchesAppendWhenNotCurrentFile_data();
  void testComposeMatchesAppendWhenNotCurrentFile();
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

void TestLinkInsertUtils::testComposeRelativeLink_data() {
  QTest::addColumn<QString>("link");
  QTest::addColumn<QString>("fragment");
  QTest::addColumn<bool>("isCurrentFile");
  QTest::addColumn<QString>("expected");

  // Same file + anchor: the file part is dropped.
  QTest::newRow("same file with anchor collapses to fragment")
      << "FileName.md" << "My-Heading" << true << "#My-Heading";
  QTest::newRow("same file with dotted relative prefix collapses")
      << "./FileName.md" << "Section-1" << true << "#Section-1";
  QTest::newRow("same file with encoded fragment collapses verbatim")
      << "a%20b.md" << "My%20Heading" << true << "#My%20Heading";
  QTest::newRow("same file with CJK anchor collapses")
      << "note.md" << QString::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87") << true
      << QString::fromUtf8("#\xE4\xB8\xAD\xE6\x96\x87");

  // Same file but NO anchor: keep the file, a bare "#" is not a usable link.
  QTest::newRow("same file without anchor keeps the file part")
      << "FileName.md" << "" << true << "FileName.md";

  // Different file: unchanged behaviour regardless of anchor.
  QTest::newRow("other file with anchor keeps the file part")
      << "Other.md" << "My-Heading" << false << "Other.md#My-Heading";
  QTest::newRow("other file without anchor is untouched")
      << "notes/Other.md" << "" << false << "notes/Other.md";
  QTest::newRow("parent-relative other file with anchor")
      << "../sub/Other.md" << "Section-1" << false << "../sub/Other.md#Section-1";
}

void TestLinkInsertUtils::testComposeRelativeLink() {
  QFETCH(QString, link);
  QFETCH(QString, fragment);
  QFETCH(bool, isCurrentFile);
  QFETCH(QString, expected);

  QCOMPARE(LinkInsertUtils::composeRelativeLink(link, fragment, isCurrentFile), expected);
}

void TestLinkInsertUtils::testComposeMatchesAppendWhenNotCurrentFile_data() {
  testAppendFragment_data();
}

void TestLinkInsertUtils::testComposeMatchesAppendWhenNotCurrentFile() {
  QFETCH(QString, link);
  QFETCH(QString, fragment);

  // The new helper must be a strict superset: for a target that is not the
  // current file it has to behave exactly like appendFragmentToLink.
  QCOMPARE(LinkInsertUtils::composeRelativeLink(link, fragment, false),
           LinkInsertUtils::appendFragmentToLink(link, fragment));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestLinkInsertUtils)
#include "test_linkinsertutils.moc"
