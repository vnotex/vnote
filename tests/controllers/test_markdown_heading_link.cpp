// Tests for the edit-mode "Copy Link" heading action seams.
//
// Both seams under test are pure static helpers on MarkdownEditorController, so
// neither a GUI nor a web view is required:
//
//   * MarkdownEditorController::isLinkableHeadingLine - the context-menu gate
//     that decides whether the clicked block is an ATX heading worth offering a
//     "Copy Link" action for.
//   * MarkdownEditorController::composeHeadingLink - composition of the final
//     absolute file:// link from the note path and the web-resolved anchor.
//
// The anchor itself is computed on the web side (markdownit.js getHeadingAnchor)
// and therefore cannot be unit-tested here; see the plan's manual parity matrix.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QUrl>

#include <controllers/markdowneditorcontroller.h>
#include <utils/pathutils.h>

using namespace vnotex;

namespace tests {

namespace {
using Heading = MarkdownEditorController::HeadingBlockInfo;

Heading heading(const QString &p_text, const QString &p_source, int p_level) {
  Heading result;
  result.level = p_level;
  result.startPos = p_text.indexOf(p_source);
  result.endPos = result.startPos + p_source.size();
  return result;
}
} // namespace

class TestMarkdownHeadingLink : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  void gateAcceptsPlainHeading();
  void gateRejectsEmptyHeading();
  void gateRejectsTooManyHashes();
  void gateRejectsPlainText();
  void gateIndentedHeading();
  void gateAcceptsAtxClosingSequence();
  void gateAcceptsAllLevels();
  void gateRejectsSetextUnderline();
  void gateAcceptsSequenceNumberedHeading();

  void composeAsciiAnchor();
  void composeHyphenatedAnchor();
  void composeCjkAnchor();
  void composePercentAnchor();
  void composePathWithSpaces();
  void composeEmptyAnchor();
  void composeNullAnchor();
  void anchorSurvivesClipboardReparse();

  void reorderMovesNestedSectionAndRelevels();
  void reorderPlacesSiblingBeforeTarget();
  void reorderPreservesAtxAndSetextSyntax();
  void reorderHandlesDocumentBoundaries_data();
  void reorderHandlesDocumentBoundaries();
  void reorderRejectsInvalidRequests();
  void reorderMapsCursorAndOverriddenSelection();
  void reorderUndoRedoIsSingleStep();

private:
  // Create an existing file under the temp dir so that PathUtils::pathToUrl
  // takes the local-file branch (it falls back to a raw QUrl for paths that do
  // not exist on disk).
  QString makeNote(const QString &p_relPath);

  QTemporaryDir m_dir;
};

void TestMarkdownHeadingLink::initTestCase() {
  QVERIFY2(m_dir.isValid(), "failed to create the temporary directory");
}

QString TestMarkdownHeadingLink::makeNote(const QString &p_relPath) {
  const QString path = QDir(m_dir.path()).filePath(p_relPath);
  const QString parent = QFileInfo(path).path();
  QDir().mkpath(parent);

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return QString();
  }
  file.write("# Heading\n");
  file.close();

  return path;
}

// ============ Gate ============

void TestMarkdownHeadingLink::gateAcceptsPlainHeading() {
  QVERIFY(MarkdownEditorController::isLinkableHeadingLine(QStringLiteral("## Overview")));
}

void TestMarkdownHeadingLink::gateRejectsEmptyHeading() {
  // An empty heading would slug to the empty string and produce a bare '#'.
  QVERIFY(!MarkdownEditorController::isLinkableHeadingLine(QStringLiteral("## ")));
  QVERIFY(!MarkdownEditorController::isLinkableHeadingLine(QStringLiteral("##")));
}

void TestMarkdownHeadingLink::gateRejectsTooManyHashes() {
  QVERIFY(!MarkdownEditorController::isLinkableHeadingLine(QStringLiteral("####### Seven")));
}

void TestMarkdownHeadingLink::gateRejectsPlainText() {
  QVERIFY(!MarkdownEditorController::isLinkableHeadingLine(QStringLiteral("text")));
  QVERIFY(!MarkdownEditorController::isLinkableHeadingLine(QString()));
}

void TestMarkdownHeadingLink::gateIndentedHeading() {
  // Pin current behaviour: the header regexp is anchored at the start of the
  // line, so leading whitespace makes it a non-heading.
  QVERIFY(!MarkdownEditorController::isLinkableHeadingLine(QStringLiteral("   ## Indented")));
}

void TestMarkdownHeadingLink::gateAcceptsAtxClosingSequence() {
  // The gate accepts it; the trailing '##' is stripped on the web side, which
  // is exactly why the anchor is resolved there rather than in C++.
  QVERIFY(MarkdownEditorController::isLinkableHeadingLine(QStringLiteral("## Foo ##")));
}

void TestMarkdownHeadingLink::gateAcceptsAllLevels() {
  for (int level = 1; level <= 6; ++level) {
    const QString line = QString(level, QLatin1Char('#')) + QStringLiteral(" Title");
    QVERIFY2(MarkdownEditorController::isLinkableHeadingLine(line), qPrintable(line));
  }
}

void TestMarkdownHeadingLink::gateRejectsSetextUnderline() {
  QVERIFY(!MarkdownEditorController::isLinkableHeadingLine(QStringLiteral("Title")));
  QVERIFY(!MarkdownEditorController::isLinkableHeadingLine(QStringLiteral("=====")));
}

void TestMarkdownHeadingLink::gateAcceptsSequenceNumberedHeading() {
  QVERIFY(MarkdownEditorController::isLinkableHeadingLine(QStringLiteral("## 1.2. Section")));
}

// ============ Link composition ============

void TestMarkdownHeadingLink::composeAsciiAnchor() {
  const QString path = makeNote(QStringLiteral("note.md"));
  QVERIFY(!path.isEmpty());

  const QString link =
      MarkdownEditorController::composeHeadingLink(path, QStringLiteral("overview"));

  QVERIFY(link.startsWith(QStringLiteral("file://")));
  QCOMPARE(QUrl(link).fragment(), QStringLiteral("overview"));
  QCOMPARE(QUrl(link).toLocalFile(), path);
}

void TestMarkdownHeadingLink::composeHyphenatedAnchor() {
  const QString path = makeNote(QStringLiteral("note-hyphen.md"));
  QVERIFY(!path.isEmpty());

  const QString link =
      MarkdownEditorController::composeHeadingLink(path, QStringLiteral("see-docs-1"));

  QVERIFY(link.endsWith(QStringLiteral("#see-docs-1")));
  QCOMPARE(QUrl(link).fragment(), QStringLiteral("see-docs-1"));
}

void TestMarkdownHeadingLink::composeCjkAnchor() {
  const QString path = makeNote(QStringLiteral("note-cjk.md"));
  QVERIFY(!path.isEmpty());

  const QString anchor = QString::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87\xE6\xA0\x87\xE9\xA2\x98");
  const QString link = MarkdownEditorController::composeHeadingLink(path, anchor);

  // Round-trips regardless of whether toString() percent-encodes the fragment.
  QCOMPARE(QUrl(link).fragment(QUrl::FullyDecoded), anchor);
}

void TestMarkdownHeadingLink::composePercentAnchor() {
  const QString path = makeNote(QStringLiteral("note-percent.md"));
  QVERIFY(!path.isEmpty());

  // A literal '%' must survive the round trip rather than being mistaken for
  // the start of a percent escape.
  const QString anchor = QStringLiteral("100%-done");
  const QString link = MarkdownEditorController::composeHeadingLink(path, anchor);

  QCOMPARE(QUrl(link).fragment(QUrl::FullyDecoded), anchor);
}

void TestMarkdownHeadingLink::composePathWithSpaces() {
  const QString path = makeNote(QStringLiteral("my notes/a note.md"));
  QVERIFY(!path.isEmpty());

  const QString link =
      MarkdownEditorController::composeHeadingLink(path, QStringLiteral("overview"));

  const QUrl url(link);
  QCOMPARE(url.toLocalFile(), path);
  QCOMPARE(url.fragment(), QStringLiteral("overview"));
}

void TestMarkdownHeadingLink::composeEmptyAnchor() {
  const QString path = makeNote(QStringLiteral("note-empty.md"));
  QVERIFY(!path.isEmpty());

  // The web side returns '' (a non-null empty QString) for a found heading
  // whose slug is empty, which is exactly why the API carries a separate
  // 'found' flag. Pin that this yields a present-but-empty fragment.
  const QString link = MarkdownEditorController::composeHeadingLink(path, QStringLiteral(""));

  QVERIFY(link.endsWith(QLatin1Char('#')));
  QVERIFY(QUrl(link).hasFragment());
  QCOMPARE(QUrl(link).toLocalFile(), path);
}

void TestMarkdownHeadingLink::composeNullAnchor() {
  const QString path = makeNote(QStringLiteral("note-null.md"));
  QVERIFY(!path.isEmpty());

  // A null QString removes the fragment entirely: no trailing '#'.
  const QString link = MarkdownEditorController::composeHeadingLink(path, QString());

  QVERIFY(!link.endsWith(QLatin1Char('#')));
  QVERIFY(!QUrl(link).hasFragment());
  QCOMPARE(QUrl(link).toLocalFile(), path);
}

void TestMarkdownHeadingLink::anchorSurvivesClipboardReparse() {
  // The composed link is NOT written to the clipboard verbatim:
  // ClipboardUtils::setLinkToClipboard feeds it back through
  // PathUtils::pathToUrl before serializing the clipboard text. Guard that
  // this re-parse preserves the anchor for the awkward anchor shapes.
  const QString path = makeNote(QStringLiteral("reparse/a note.md"));
  QVERIFY(!path.isEmpty());

  const QStringList anchors = {
      QStringLiteral("overview"), QStringLiteral("see-docs-1"), QStringLiteral("100%-done"),
      QString::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87\xE6\xA0\x87\xE9\xA2\x98")};

  for (const auto &anchor : anchors) {
    const QString link = MarkdownEditorController::composeHeadingLink(path, anchor);
    const QUrl reparsed = PathUtils::pathToUrl(link);

    QVERIFY2(reparsed.isLocalFile(), qPrintable(link));
    QCOMPARE(reparsed.toLocalFile(), path);
    QCOMPARE(reparsed.fragment(QUrl::FullyDecoded), anchor);
  }
}

void TestMarkdownHeadingLink::reorderMovesNestedSectionAndRelevels() {
  const QString original =
      QStringLiteral("# A\nA body\n## Child\nC body\n### Grand\nG body\n# B\nB body");
  QTextDocument document(original);
  const QVector<Heading> headings = {heading(original, QStringLiteral("# A"), 1),
                                     heading(original, QStringLiteral("## Child"), 2),
                                     heading(original, QStringLiteral("### Grand"), 3),
                                     heading(original, QStringLiteral("# B"), 1)};

  const auto result =
      MarkdownEditorController::reorderHeadingBlock(&document, headings, 1, -1, 2, 0, 0, -1, -1);

  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(),
           QStringLiteral("# A\nA body\n# B\nB body\n## Child\nC body\n### Grand\nG body"));
}

void TestMarkdownHeadingLink::reorderPlacesSiblingBeforeTarget() {
  const QString original = QStringLiteral("# A\n## One\none\n## Two\ntwo\n# End");
  QTextDocument document(original);
  const QVector<Heading> headings = {heading(original, QStringLiteral("# A"), 1),
                                     heading(original, QStringLiteral("## One"), 2),
                                     heading(original, QStringLiteral("## Two"), 2),
                                     heading(original, QStringLiteral("# End"), 1)};

  const auto result =
      MarkdownEditorController::reorderHeadingBlock(&document, headings, 2, 1, 2, 0, 0, -1, -1);

  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(), QStringLiteral("# A\n## Two\ntwo\n## One\none\n# End"));
}

void TestMarkdownHeadingLink::reorderPreservesAtxAndSetextSyntax() {
  {
    const QString original = QStringLiteral("  ## *Child* ###");
    QTextDocument document(original);
    const QVector<Heading> headings = {heading(original, QStringLiteral("  ## *Child* ###"), 2)};
    const auto result =
        MarkdownEditorController::reorderHeadingBlock(&document, headings, 0, -1, 4, 0, 0, -1, -1);
    QVERIFY(result.moved);
    QCOMPARE(document.toPlainText(), QStringLiteral("  #### *Child* ###"));
  }

  {
    const QString original = QStringLiteral("One\n===\nTwo\n---\n# End");
    QTextDocument document(original);
    const QVector<Heading> headings = {heading(original, QStringLiteral("One\n==="), 1),
                                       heading(original, QStringLiteral("Two\n---"), 2),
                                       heading(original, QStringLiteral("# End"), 1)};
    const auto result =
        MarkdownEditorController::reorderHeadingBlock(&document, headings, 0, 2, 2, 0, 0, -1, -1);
    QVERIFY(result.moved);
    QCOMPARE(document.toPlainText(), QStringLiteral("One\n---\n### Two\n# End"));
  }

  {
    const QString original = QStringLiteral("Title\n---");
    QTextDocument document(original);
    const QVector<Heading> headings = {heading(original, QStringLiteral("Title\n---"), 2)};
    const auto result =
        MarkdownEditorController::reorderHeadingBlock(&document, headings, 0, -1, 1, 0, 0, -1, -1);
    QVERIFY(result.moved);
    QCOMPARE(document.toPlainText(), QStringLiteral("Title\n==="));
  }
}

void TestMarkdownHeadingLink::reorderHandlesDocumentBoundaries_data() {
  QTest::addColumn<QString>("original");
  QTest::addColumn<int>("source");
  QTest::addColumn<int>("before");
  QTest::addColumn<QString>("expected");

  QTest::newRow("first-to-last-unterminated")
      << QStringLiteral("# A\na\n# B\nb") << 0 << -1 << QStringLiteral("# B\nb\n# A\na");
  QTest::newRow("first-to-last-terminated")
      << QStringLiteral("# A\na\n# B\nb\n") << 0 << -1 << QStringLiteral("# B\nb\n# A\na\n");
  QTest::newRow("last-to-first-unterminated")
      << QStringLiteral("# A\na\n# B\nb") << 1 << 0 << QStringLiteral("# B\nb\n# A\na");
  QTest::newRow("last-to-first-terminated")
      << QStringLiteral("# A\na\n# B\nb\n") << 1 << 0 << QStringLiteral("# B\nb\n# A\na\n");
}

void TestMarkdownHeadingLink::reorderHandlesDocumentBoundaries() {
  QFETCH(QString, original);
  QFETCH(int, source);
  QFETCH(int, before);
  QFETCH(QString, expected);
  QTextDocument document(original);
  const QVector<Heading> headings = {heading(original, QStringLiteral("# A"), 1),
                                     heading(original, QStringLiteral("# B"), 1)};

  const auto result = MarkdownEditorController::reorderHeadingBlock(&document, headings, source,
                                                                    before, 1, 0, 0, -1, -1);

  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(), expected);
}

void TestMarkdownHeadingLink::reorderRejectsInvalidRequests() {
  const QString original = QStringLiteral("# A\n## Child\n### Deep\n# B");
  const QVector<Heading> headings = {heading(original, QStringLiteral("# A"), 1),
                                     heading(original, QStringLiteral("## Child"), 2),
                                     heading(original, QStringLiteral("### Deep"), 3),
                                     heading(original, QStringLiteral("# B"), 1)};

  auto rejected = [&](const QVector<Heading> &p_headings, int p_source, int p_before, int p_level) {
    QTextDocument document(original);
    const auto result = MarkdownEditorController::reorderHeadingBlock(
        &document, p_headings, p_source, p_before, p_level, 0, 0, -1, -1);
    QVERIFY(!result.moved);
    QCOMPARE(document.toPlainText(), original);
  };

  rejected(headings, -1, -1, 1);
  rejected(headings, 0, 1, 1);
  rejected(headings, 1, 1, 2);
  rejected(headings, 1, 3, 2);

  QVector<Heading> stale = headings;
  stale[1].level = 3;
  rejected(stale, 1, -1, 2);

  const QString deepText = QStringLiteral("##### Five\n###### Six\n# End");
  QTextDocument deepDocument(deepText);
  const QVector<Heading> deepHeadings = {heading(deepText, QStringLiteral("##### Five"), 5),
                                         heading(deepText, QStringLiteral("###### Six"), 6),
                                         heading(deepText, QStringLiteral("# End"), 1)};
  const auto overflow = MarkdownEditorController::reorderHeadingBlock(&deepDocument, deepHeadings,
                                                                      0, -1, 6, 0, 0, -1, -1);
  QVERIFY(!overflow.moved);
  QCOMPARE(deepDocument.toPlainText(), deepText);
}

void TestMarkdownHeadingLink::reorderMapsCursorAndOverriddenSelection() {
  const QString original = QStringLiteral("# A\nbody\n# B\nend");
  QTextDocument document(original);
  const QVector<Heading> headings = {heading(original, QStringLiteral("# A"), 1),
                                     heading(original, QStringLiteral("# B"), 1)};
  const int bodyStart = original.indexOf(QStringLiteral("body"));
  const int endPosition = original.indexOf(QStringLiteral("end")) + 1;

  const auto result = MarkdownEditorController::reorderHeadingBlock(
      &document, headings, 0, -1, 1, bodyStart + 1, endPosition, bodyStart, bodyStart + 4);

  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(), QStringLiteral("# B\nend\n# A\nbody"));
  QCOMPARE(document.toPlainText().at(result.cursorPosition), QLatin1Char('o'));
  QCOMPARE(document.toPlainText().at(result.cursorAnchor), QLatin1Char('n'));
  QCOMPARE(document.toPlainText().mid(result.selectionStart,
                                      result.selectionEnd - result.selectionStart),
           QStringLiteral("body"));
}

void TestMarkdownHeadingLink::reorderUndoRedoIsSingleStep() {
  const QString original = QStringLiteral("# A\na\n# B\nb");
  const QString reordered = QStringLiteral("# B\nb\n# A\na");
  QTextDocument document(original);
  document.clearUndoRedoStacks();
  const QVector<Heading> headings = {heading(original, QStringLiteral("# A"), 1),
                                     heading(original, QStringLiteral("# B"), 1)};

  const auto result =
      MarkdownEditorController::reorderHeadingBlock(&document, headings, 0, -1, 1, 0, 0, -1, -1);
  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(), reordered);
  QVERIFY(document.isUndoAvailable());

  document.undo();
  QCOMPARE(document.toPlainText(), original);
  QVERIFY(!document.isUndoAvailable());
  document.redo();
  QCOMPARE(document.toPlainText(), reordered);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMarkdownHeadingLink)
#include "test_markdown_heading_link.moc"
