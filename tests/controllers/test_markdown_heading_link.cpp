// Tests for heading links and Markdown/Typst source section reordering.
//
// The link seams are pure static helpers on MarkdownEditorController, and
// OutlineSectionEditor reorders QTextDocument sections without a GUI or web view:
//
//   * MarkdownEditorController::isLinkableHeadingLine - the context-menu gate
//     that decides whether the clicked block is an ATX heading worth offering a
//     "Copy Link" action for.
//   * MarkdownEditorController::composeHeadingLink - composition of the final
//     absolute file:// link from the note path and the native-resolved anchor.
//
// Native anchor semantics and async lifetime are covered by
// test_markdownhtmlrenderer and test_markdownrendercontroller.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QUrl>

#include <limits>

#include <controllers/markdowneditorcontroller.h>
#include <controllers/outlinesectioneditor.h>
#include <utils/pathutils.h>
#include <vtextedit/markdowndocument.h>

using namespace vnotex;

namespace tests {

namespace {
using Heading = OutlineSectionEditor::HeadingBlockInfo;

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
  void typstReorderPreservesDeepHierarchy();
  void typstReorderMapsUnicodeSelection();
  void typstReorderHandlesDocumentBoundaries_data();
  void typstReorderHandlesDocumentBoundaries();
  void typstReorderCarriesHeadingIndentation();
  void typstReorderUndoRedoIsSingleStep();
  void typstReorderRejectsInvalidRequests();
  void renameHeadingPreservesSource_data();
  void renameHeadingPreservesSource();
  void insertHeadingPreservesSectionBodies();
  void headingEditsRejectInvalidSnapshots();
  void reorderReturnsHeadingPositions();
  void setextParserBoundaries_data();
  void setextParserBoundaries();
  void setextParserTitleBoundaries_data();
  void setextParserTitleBoundaries();
  void setextParserOverlapsRemainRejected();
  void typstRenamePreservesCertifiedRanges();

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
  // The native parser strips the closing sequence before generating the anchor.
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
      OutlineSectionEditor::reorderHeadingBlock(&document, headings, 1, -1, 2, 0, 0, -1, -1);

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
      OutlineSectionEditor::reorderHeadingBlock(&document, headings, 2, 1, 2, 0, 0, -1, -1);

  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(), QStringLiteral("# A\n## Two\ntwo\n## One\none\n# End"));
}

void TestMarkdownHeadingLink::reorderPreservesAtxAndSetextSyntax() {
  {
    const QString original = QStringLiteral("  ## *Child* ###");
    QTextDocument document(original);
    const QVector<Heading> headings = {heading(original, QStringLiteral("  ## *Child* ###"), 2)};
    const auto result =
        OutlineSectionEditor::reorderHeadingBlock(&document, headings, 0, -1, 4, 0, 0, -1, -1);
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
        OutlineSectionEditor::reorderHeadingBlock(&document, headings, 0, 2, 2, 0, 0, -1, -1);
    QVERIFY(result.moved);
    QCOMPARE(document.toPlainText(), QStringLiteral("One\n---\n### Two\n# End"));
  }

  {
    const QString original = QStringLiteral("Title\n---");
    QTextDocument document(original);
    const QVector<Heading> headings = {heading(original, QStringLiteral("Title\n---"), 2)};
    const auto result =
        OutlineSectionEditor::reorderHeadingBlock(&document, headings, 0, -1, 1, 0, 0, -1, -1);
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

  const auto result = OutlineSectionEditor::reorderHeadingBlock(&document, headings, source, before,
                                                                1, 0, 0, -1, -1);

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
    const auto result = OutlineSectionEditor::reorderHeadingBlock(&document, p_headings, p_source,
                                                                  p_before, p_level, 0, 0, -1, -1);
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
  const auto overflow = OutlineSectionEditor::reorderHeadingBlock(&deepDocument, deepHeadings, 0,
                                                                  -1, 6, 0, 0, -1, -1);
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

  const auto result = OutlineSectionEditor::reorderHeadingBlock(
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
      OutlineSectionEditor::reorderHeadingBlock(&document, headings, 0, -1, 1, 0, 0, -1, -1);
  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(), reordered);
  QVERIFY(document.isUndoAvailable());

  document.undo();
  QCOMPARE(document.toPlainText(), original);
  QVERIFY(!document.isUndoAvailable());
  document.redo();
  QCOMPARE(document.toPlainText(), reordered);
}

void TestMarkdownHeadingLink::typstReorderPreservesDeepHierarchy() {
  const QString original = QStringLiteral(
      "= A\nbody A\n== Child\nchild\n======== Deep\ndeep\n= B\n========= Existing\nstay");
  QTextDocument document(original);
  const QVector<Heading> headings = {heading(original, QStringLiteral("= A"), 1),
                                     heading(original, QStringLiteral("== Child"), 2),
                                     heading(original, QStringLiteral("======== Deep"), 8),
                                     heading(original, QStringLiteral("= B"), 1),
                                     heading(original, QStringLiteral("========= Existing"), 9)};

  const auto result = OutlineSectionEditor::reorderHeadingBlock(
      &document, headings, 0, -1, 7, 0, 0, -1, -1, OutlineSectionEditor::Syntax::Typst);

  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(),
           QStringLiteral("= B\n========= Existing\nstay\n======= A\nbody A\n======== "
                          "Child\nchild\n============== Deep\ndeep"));
}

void TestMarkdownHeadingLink::typstReorderMapsUnicodeSelection() {
  const QString emoji = QString::fromUtf8("\xF0\x9F\xA7\xAD");
  const QString title = QStringLiteral("= First ") + emoji;
  const QString body = QStringLiteral("\u4e2d") + emoji + QStringLiteral("\u6587");
  const QString original = title + QLatin1Char('\n') + body + QStringLiteral("\n= Last\nend");
  QTextDocument document(original);
  const QVector<Heading> headings = {heading(original, title, 1),
                                     heading(original, QStringLiteral("= Last"), 1)};
  const int bodyStart = original.indexOf(body);
  const int outside = original.indexOf(QStringLiteral("end")) + 1;

  const auto result = OutlineSectionEditor::reorderHeadingBlock(
      &document, headings, 0, -1, 3, bodyStart + 1, outside, bodyStart, bodyStart + body.size(),
      OutlineSectionEditor::Syntax::Typst);

  QVERIFY(result.moved);
  const QString reordered = document.toPlainText();
  QCOMPARE(reordered, QStringLiteral("= Last\nend\n==") + title + QLatin1Char('\n') + body);
  QCOMPARE(reordered.mid(result.cursorPosition, emoji.size()), emoji);
  QCOMPARE(reordered.at(result.cursorAnchor), QLatin1Char('n'));
  QCOMPARE(reordered.mid(result.selectionStart, result.selectionEnd - result.selectionStart), body);
}

void TestMarkdownHeadingLink::typstReorderHandlesDocumentBoundaries_data() {
  QTest::addColumn<QString>("original");
  QTest::addColumn<int>("source");
  QTest::addColumn<int>("before");
  QTest::addColumn<QString>("expected");

  QTest::newRow("first-to-last-unterminated")
      << QStringLiteral("= A\na\n= B\nb") << 0 << -1 << QStringLiteral("= B\nb\n= A\na");
  QTest::newRow("first-to-last-terminated")
      << QStringLiteral("= A\na\n= B\nb\n") << 0 << -1 << QStringLiteral("= B\nb\n= A\na\n");
  QTest::newRow("last-to-first-unterminated")
      << QStringLiteral("= A\na\n= B\nb") << 1 << 0 << QStringLiteral("= B\nb\n= A\na");
  QTest::newRow("last-to-first-terminated")
      << QStringLiteral("= A\na\n= B\nb\n") << 1 << 0 << QStringLiteral("= B\nb\n= A\na\n");
  QTest::newRow("preamble-last-to-first") << QStringLiteral("// lead\n= A\na\n= B\nb") << 1 << 0
                                          << QStringLiteral("// lead\n= B\nb\n= A\na");
}

void TestMarkdownHeadingLink::typstReorderHandlesDocumentBoundaries() {
  QFETCH(QString, original);
  QFETCH(int, source);
  QFETCH(int, before);
  QFETCH(QString, expected);
  QTextDocument document(original);
  const QVector<Heading> headings = {heading(original, QStringLiteral("= A"), 1),
                                     heading(original, QStringLiteral("= B"), 1)};

  const auto result = OutlineSectionEditor::reorderHeadingBlock(
      &document, headings, source, before, 1, 0, 0, -1, -1, OutlineSectionEditor::Syntax::Typst);

  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(), expected);
}

void TestMarkdownHeadingLink::typstReorderCarriesHeadingIndentation() {
  const QString original = QStringLiteral("  = A\na\n\t= B\nb");
  QTextDocument document(original);
  const QVector<Heading> headings = {heading(original, QStringLiteral("= A"), 1),
                                     heading(original, QStringLiteral("= B"), 1)};

  const auto result = OutlineSectionEditor::reorderHeadingBlock(
      &document, headings, 1, 0, 2, 0, 0, -1, -1, OutlineSectionEditor::Syntax::Typst);

  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(), QStringLiteral("\t== B\nb\n  = A\na"));
}

void TestMarkdownHeadingLink::typstReorderUndoRedoIsSingleStep() {
  const QString original = QStringLiteral("= A\na\n== Child\nc\n= B\nb\n");
  const QString reordered = QStringLiteral("= B\nb\n======= A\na\n======== Child\nc\n");
  QTextDocument document(original);
  document.clearUndoRedoStacks();
  const QVector<Heading> headings = {heading(original, QStringLiteral("= A"), 1),
                                     heading(original, QStringLiteral("== Child"), 2),
                                     heading(original, QStringLiteral("= B"), 1)};

  const auto result = OutlineSectionEditor::reorderHeadingBlock(
      &document, headings, 0, -1, 7, 0, 0, -1, -1, OutlineSectionEditor::Syntax::Typst);

  QVERIFY(result.moved);
  QCOMPARE(document.toPlainText(), reordered);
  QVERIFY(document.isUndoAvailable());
  document.undo();
  QCOMPARE(document.toPlainText(), original);
  QVERIFY(!document.isUndoAvailable());
  document.redo();
  QCOMPARE(document.toPlainText(), reordered);
  QVERIFY(!document.isRedoAvailable());
}

void TestMarkdownHeadingLink::typstReorderRejectsInvalidRequests() {
  const QString original = QStringLiteral("= A\n== Child\n= B");
  const QVector<Heading> headings = {heading(original, QStringLiteral("= A"), 1),
                                     heading(original, QStringLiteral("== Child"), 2),
                                     heading(original, QStringLiteral("= B"), 1)};
  const auto rejected = [&](const QString &p_text, const QVector<Heading> &p_headings, int p_source,
                            int p_before, int p_level,
                            OutlineSectionEditor::Syntax p_syntax =
                                OutlineSectionEditor::Syntax::Typst) {
    QTextDocument document(p_text);
    document.clearUndoRedoStacks();
    const auto result = OutlineSectionEditor::reorderHeadingBlock(
        &document, p_headings, p_source, p_before, p_level, 2, 1, 0, 1, p_syntax);
    QVERIFY(!result.moved);
    QCOMPARE(document.toPlainText(), p_text);
    QCOMPARE(result.cursorPosition, 2);
    QCOMPARE(result.cursorAnchor, 1);
    QCOMPARE(result.selectionStart, 0);
    QCOMPARE(result.selectionEnd, 1);
    QVERIFY(!document.isUndoAvailable());
  };

  rejected(original, headings, -1, -1, 1);
  rejected(original, headings, 0, -2, 1);
  rejected(original, headings, 0, 3, 1);
  rejected(original, headings, 0, 1, 1);
  rejected(original, headings, 0, 2, 1); // No movement or level change.
  rejected(original, headings, 0, -1, 0);
  rejected(original, headings, 0, -1, std::numeric_limits<int>::max()); // Descendant overflow.
  rejected(original, headings, 2, 0, std::numeric_limits<int>::max());  // Document size overflow.
  rejected(original, headings, 0, -1, 2, static_cast<OutlineSectionEditor::Syntax>(-1));
  rejected(original, headings, 0, -1, 2, OutlineSectionEditor::Syntax::Markdown);

  QVector<Heading> invalid = headings;
  invalid[1].level = 3;
  rejected(original, invalid, 0, -1, 2);
  invalid = headings;
  invalid[1].endPos = original.size() + 1;
  rejected(original, invalid, 0, -1, 2);
  invalid = headings;
  invalid[1].startPos = invalid[0].startPos;
  rejected(original, invalid, 0, -1, 2);

  const QString markdown = QStringLiteral("# A\n# B");
  rejected(
      markdown,
      {heading(markdown, QStringLiteral("# A"), 1), heading(markdown, QStringLiteral("# B"), 1)}, 0,
      -1, 2);
  const QString setext = QStringLiteral("Title\n===\n= B");
  rejected(
      setext,
      {heading(setext, QStringLiteral("Title\n==="), 1), heading(setext, QStringLiteral("= B"), 1)},
      0, -1, 2);
  const QString missingSpace = QStringLiteral("=A\n= B");
  rejected(missingSpace,
           {heading(missingSpace, QStringLiteral("=A"), 1),
            heading(missingSpace, QStringLiteral("= B"), 1)},
           0, -1, 2);
  const QString functional = QStringLiteral("#heading[Title]\n= B");
  rejected(functional,
           {heading(functional, QStringLiteral("#heading[Title]"), 1),
            heading(functional, QStringLiteral("= B"), 1)},
           0, -1, 2);
  const QString inlineHeading = QStringLiteral("text = A\n= B");
  rejected(inlineHeading,
           {heading(inlineHeading, QStringLiteral("= A"), 1),
            heading(inlineHeading, QStringLiteral("= B"), 1)},
           0, -1, 2);
}

void TestMarkdownHeadingLink::renameHeadingPreservesSource_data() {
  QTest::addColumn<QString>("original");
  QTest::addColumn<QString>("headingText");
  QTest::addColumn<int>("level");
  QTest::addColumn<QString>("topic");
  QTest::addColumn<QString>("expected");
  QTest::newRow("atx-literal")
      << QStringLiteral("Preamble\n\n  ## *Old* ###\n\nBody Ω\n")
      << QStringLiteral("  ## *Old* ###") << 2 << QStringLiteral("Renamed # [x] & Ω")
      << QStringLiteral("Preamble\n\n  ## Renamed &#35; &#91;x&#93; &#38; Ω ###\n\nBody Ω\n");
  QTest::newRow("atx-parser-marker-span")
      << QStringLiteral("  ## Old\nbody") << QStringLiteral("## Old") << 2 << QStringLiteral("New")
      << QStringLiteral("  ## New\nbody");
  QTest::newRow("atx-container-span")
      << QStringLiteral("> ## Old\n> body") << QStringLiteral("## Old") << 2
      << QStringLiteral("New") << QStringLiteral("> ## New\n> body");
  QTest::newRow("atx-number-prefix")
      << QStringLiteral("## 1. Old\nbody") << QStringLiteral("## 1. Old") << 2
      << QStringLiteral("1. New") << QStringLiteral("## 1. New\nbody");
  QTest::newRow("atx-empty") << QStringLiteral("## Old ##\nbody") << QStringLiteral("## Old ##")
                             << 2 << QString() << QStringLiteral("##  ##\nbody");
  QTest::newRow("atx-bare") << QStringLiteral("##\nbody") << QStringLiteral("##") << 2
                            << QStringLiteral(" New ") << QStringLiteral("## &#32;New&#32;\nbody");
  QTest::newRow("atx-closing-only")
      << QStringLiteral("## ###\nbody") << QStringLiteral("## ###") << 2 << QStringLiteral("New")
      << QStringLiteral("## New ###\nbody");
  QTest::newRow("setext") << QStringLiteral("Preamble\n\n  Old *title*\n---\nbody")
                          << QStringLiteral("  Old *title*\n---") << 2
                          << QStringLiteral("New `title`")
                          << QStringLiteral("Preamble\n\n  New &#96;title&#96;\n---\nbody");
  QTest::newRow("setext-list-like")
      << QStringLiteral("Old\n===\nbody") << QStringLiteral("Old\n===") << 1
      << QStringLiteral("1. [item]") << QStringLiteral("1&#46; &#91;item&#93;\n===\nbody");
  QTest::newRow("setext-empty") << QStringLiteral("Old\n===\nbody") << QStringLiteral("Old\n===")
                                << 1 << QString() << QStringLiteral("#\nbody");
  QTest::newRow("literal-syntax")
      << QStringLiteral("# Old\nbody") << QStringLiteral("# Old") << 1
      << QStringLiteral("\\*x* <b> ![a](b) _c_ ~d~ |e| {f}")
      << QStringLiteral("# &#92;&#42;x&#42; &#60;b&#62; &#33;&#91;a&#93;&#40;b) &#95;c&#95; "
                        "&#126;d&#126; &#124;e&#124; &#123;f&#125;\nbody");
}

void TestMarkdownHeadingLink::renameHeadingPreservesSource() {
  QFETCH(QString, original);
  QFETCH(QString, headingText);
  QFETCH(int, level);
  QFETCH(QString, topic);
  QFETCH(QString, expected);
  QTextDocument document(original);
  document.clearUndoRedoStacks();
  const auto sourceHeading = heading(original, headingText, level);
  const auto result = OutlineSectionEditor::renameHeading(&document, {sourceHeading}, 0, topic,
                                                          original.size(), sourceHeading.startPos,
                                                          original.size() - 2, original.size());
  QVERIFY(result.changed);
  QCOMPARE(document.toPlainText(), expected);
  QCOMPARE(result.cursorPosition, expected.size());
  QCOMPARE(result.cursorAnchor, sourceHeading.startPos);
  QCOMPARE(result.selectionStart, expected.size() - 2);
  QCOMPARE(result.selectionEnd, expected.size());
  QCOMPARE(result.headingPositions, QVector<int>{sourceHeading.startPos});
  document.undo();
  QCOMPARE(document.toPlainText(), original);
  QVERIFY(!document.isUndoAvailable());
  document.redo();
  QCOMPARE(document.toPlainText(), expected);
}

void TestMarkdownHeadingLink::insertHeadingPreservesSectionBodies() {
  const QString original = QStringLiteral(
      "Preamble\n\n# Book\nIntro\n## Alpha\nA body\n### Child\nC body\n## Beta\nB body");
  const QVector<Heading> headings = {heading(original, QStringLiteral("# Book"), 1),
                                     heading(original, QStringLiteral("## Alpha"), 2),
                                     heading(original, QStringLiteral("### Child"), 3),
                                     heading(original, QStringLiteral("## Beta"), 2)};
  for (const bool append : {false, true}) {
    QTextDocument document(original);
    document.clearUndoRedoStacks();
    const auto result =
        OutlineSectionEditor::insertHeading(&document, headings, append ? -1 : 3, append ? 0 : 1,
                                            append ? 2 : 3, QString(), original.size(), 0, -1, -1);
    QVERIFY(result.changed);
    const QString inserted = append ? QStringLiteral("\n\n## \n") : QStringLiteral("\n### \n\n");
    const int position = append ? original.size() : headings[3].startPos;
    QString expected = original;
    expected.insert(position, inserted);
    QCOMPARE(document.toPlainText(), expected);
    QCOMPARE(result.cursorPosition, append ? original.size() : expected.size());
    QCOMPARE(result.headingPositions.back(), position + (append ? 2 : 1));
    QCOMPARE(result.headingPositions[3],
             append ? headings[3].startPos : headings[3].startPos + inserted.size());
    document.undo();
    QCOMPARE(document.toPlainText(), original);
    QVERIFY(!document.isUndoAvailable());
  }
  QTextDocument beforeFirst(original);
  const auto first = OutlineSectionEditor::insertHeading(&beforeFirst, headings, 0, -1, 1,
                                                         QStringLiteral("First"), 0, 0, -1, -1);
  QVERIFY(first.changed);
  QCOMPARE(beforeFirst.toPlainText(),
           QStringLiteral("Preamble\n\n# First\n\n") + original.mid(headings[0].startPos));
}

void TestMarkdownHeadingLink::headingEditsRejectInvalidSnapshots() {
  const QString original = QStringLiteral("# Book\n## Alpha\nbody\n## Beta\nend");
  const QVector<Heading> headings = {heading(original, QStringLiteral("# Book"), 1),
                                     heading(original, QStringLiteral("## Alpha"), 2),
                                     heading(original, QStringLiteral("## Beta"), 2)};
  QTextDocument document(original);
  document.clearUndoRedoStacks();
  for (const auto &topic :
       {QStringLiteral("two\nlines"), QStringLiteral("carriage\rreturn"),
        QStringLiteral("nul") + QChar(0), QString(QChar(0x2028)), QString(QChar(0x2029)),
        QString(QChar(0xD800)), QString(QChar(0xDC00))}) {
    QVERIFY(
        !OutlineSectionEditor::renameHeading(&document, headings, 1, topic, 3, 2, 1, 4).changed);
    QVERIFY(!OutlineSectionEditor::insertHeading(&document, headings, 2, 0, 2, topic, 3, 2, 1, 4)
                 .changed);
  }
  auto invalid = headings;
  invalid[2].startPos = invalid[1].startPos;
  QVERIFY(!OutlineSectionEditor::renameHeading(&document, invalid, 0, QStringLiteral("New"), 0, 0,
                                               -1, -1)
               .changed);
  invalid = headings;
  invalid[2].endPos = original.size() + 1;
  QVERIFY(
      !OutlineSectionEditor::insertHeading(&document, invalid, 1, 0, 2, {}, 0, 0, -1, -1).changed);
  QVERIFY(
      !OutlineSectionEditor::insertHeading(&document, headings, 2, 1, 2, {}, 0, 0, -1, -1).changed);
  QVERIFY(!OutlineSectionEditor::insertHeading(&document, headings, -1, 0, 7, {}, 0, 0, -1, -1)
               .changed);
  QVERIFY(!OutlineSectionEditor::renameHeading(&document, headings, 3, QStringLiteral("New"), 0, 0,
                                               -1, -1)
               .changed);
  QCOMPARE(document.toPlainText(), original);
  QVERIFY(!document.isUndoAvailable());

  const QString unicode = QString::fromUtf8("# \xf0\x9f\x98\x80\nbody");
  QTextDocument unicodeDocument(unicode);
  unicodeDocument.clearUndoRedoStacks();
  Heading splitSurrogate = heading(unicode, unicode.left(4), 1);
  splitSurrogate.endPos = 3;
  QVERIFY(!OutlineSectionEditor::renameHeading(&unicodeDocument, {splitSurrogate}, 0,
                                               QStringLiteral("New"), 0, 0, -1, -1)
               .changed);
  QCOMPARE(unicodeDocument.toPlainText(), unicode);
  QVERIFY(!unicodeDocument.isUndoAvailable());
}

void TestMarkdownHeadingLink::reorderReturnsHeadingPositions() {
  const QString original = QStringLiteral("# A\n## Child\nbody\n# B\nend");
  const QVector<Heading> headings = {heading(original, QStringLiteral("# A"), 1), Heading{},
                                     heading(original, QStringLiteral("## Child"), 2),
                                     heading(original, QStringLiteral("# B"), 1)};
  QTextDocument document(original);
  QVector<int> positions;
  const auto result =
      OutlineSectionEditor::reorderHeadingBlock(&document, headings, 0, -1, 2, 0, 0, -1, -1,
                                                OutlineSectionEditor::Syntax::Markdown, &positions);
  QVERIFY(result.moved);
  const auto text = document.toPlainText();
  QCOMPARE(positions, (QVector<int>{int(text.indexOf(QStringLiteral("## A"))), -1,
                                    int(text.indexOf(QStringLiteral("### Child"))), 0}));
}

void TestMarkdownHeadingLink::setextParserBoundaries_data() {
  QTest::addColumn<QString>("operation");
  for (const auto *operation : {"move", "append", "relevel", "rename", "empty", "insert"}) {
    QTest::newRow(operation) << QString::fromLatin1(operation);
  }
}

void TestMarkdownHeadingLink::setextParserBoundaries() {
  QFETCH(QString, operation);
  const QString preamble = QStringLiteral("Preamble Ω 😀\n\nBook\n====\n\n");
  const QString alpha = QStringLiteral("Same\n----\n\nFirst body Ω\n\n### Child\n\nChild body\n\n");
  const QString beta = QStringLiteral("Same\n----\n\nLast body\n");
  const QString original = preamble + alpha + beta;
  const int options = vte::MarkdownDocument::viewerParseOptions(true, true);
  const auto parsed = vte::MarkdownDocument::parse(original.toUtf8(), options);
  QVERIFY(parsed);
  QCOMPARE(parsed->headings().size(), 4);
  QVector<Heading> headings;
  for (const auto &item : parsed->headings()) {
    Heading info;
    info.level = item.m_level;
    info.startPos = item.m_startPos;
    info.endPos = item.m_endPos;
    headings.append(info);
  }
  // Use the real parser's UTF-16 ranges, not hand-trimmed heading snippets.
  QVERIFY(parsed->headings()[0].m_isSetext);
  QVERIFY(parsed->headings()[1].m_isSetext);
  QVERIFY(!parsed->headings()[2].m_isSetext);
  QVERIFY(parsed->headings()[3].m_isSetext);
  QCOMPARE(headings[0].startPos, original.indexOf(QStringLiteral("Book")));
  QCOMPARE(original.mid(headings[0].startPos, headings[0].endPos - headings[0].startPos),
           QStringLiteral("Book\n====\n\n"));
  QCOMPARE(original.mid(headings[1].startPos, headings[1].endPos - headings[1].startPos),
           QStringLiteral("Same\n----\n\n"));
  QCOMPARE(original.mid(headings[3].startPos, headings[3].endPos - headings[3].startPos),
           QStringLiteral("Same\n----\n\n"));

  QTextDocument document(original);
  document.clearUndoRedoStacks();
  QVector<int> positions;
  QVector<int> order{0, 1, 2, 3};
  QString expected;
  QStringList titles{QStringLiteral("Book"), QStringLiteral("Same"), QStringLiteral("Child"),
                     QStringLiteral("Same")};
  QVector<int> levels{1, 2, 3, 2};
  const int cursor = original.indexOf(QStringLiteral("Last body"));
  if (operation == QLatin1String("move") || operation == QLatin1String("append") ||
      operation == QLatin1String("relevel")) {
    const bool move = operation != QLatin1String("relevel");
    const bool append = operation == QLatin1String("append");
    const auto result = OutlineSectionEditor::reorderHeadingBlock(
        &document, headings, append ? 1 : 3, move && !append ? 1 : -1, move ? 2 : 3, cursor, cursor,
        cursor, cursor + 4, OutlineSectionEditor::Syntax::Markdown, &positions);
    QVERIFY(result.moved);
    expected = move ? preamble + beta + QLatin1Char('\n') + alpha
                    : preamble + alpha + QStringLiteral("### Same\n\nLast body\n");
    if (move) {
      order = {0, 3, 1, 2};
      titles = {QStringLiteral("Book"), QStringLiteral("Same"), QStringLiteral("Same"),
                QStringLiteral("Child")};
      levels = {1, 2, 2, 3};
    } else {
      levels[3] = 3;
    }
    QCOMPARE(result.cursorPosition, expected.indexOf(QStringLiteral("Last body")));
    QCOMPARE(result.cursorAnchor, result.cursorPosition);
    QCOMPARE(result.selectionStart, result.cursorPosition);
    QCOMPARE(result.selectionEnd, result.cursorPosition + 4);
  } else {
    OutlineSectionEditor::HeadingEditResult result;
    if (operation == QLatin1String("insert")) {
      result =
          OutlineSectionEditor::insertHeading(&document, headings, 3, 0, 2, QStringLiteral("Gamma"),
                                              cursor, cursor, cursor, cursor + 4);
      expected = preamble + alpha + QStringLiteral("## Gamma\n\n") + beta;
      order = {0, 1, 2, 4, 3};
      titles.insert(3, QStringLiteral("Gamma"));
      levels.insert(3, 2);
    } else {
      const bool empty = operation == QLatin1String("empty");
      const QString topic = empty ? QString() : QStringLiteral("Renamed # [x] & Ω");
      result = OutlineSectionEditor::renameHeading(&document, headings, 1, topic, cursor, cursor,
                                                   cursor, cursor + 4);
      expected = preamble +
                 (empty ? QStringLiteral("##")
                        : QStringLiteral("Renamed &#35; &#91;x&#93; &#38; Ω\n----")) +
                 alpha.mid(QStringLiteral("Same\n----").size()) + beta;
      titles[1] = topic;
    }
    QVERIFY(result.changed);
    positions = result.headingPositions;
    QCOMPARE(result.cursorPosition, expected.indexOf(QStringLiteral("Last body")));
    QCOMPARE(result.cursorAnchor, result.cursorPosition);
    QCOMPARE(result.selectionStart, result.cursorPosition);
    QCOMPARE(result.selectionEnd, result.cursorPosition + 4);
  }
  QCOMPARE(document.toPlainText(), expected);
  const auto reparsed = vte::MarkdownDocument::parse(expected.toUtf8(), options);
  QVERIFY(reparsed);
  QCOMPARE(reparsed->headings().size(), order.size());
  QCOMPARE(positions.size(), order.size());
  for (int i = 0; i < order.size(); ++i) {
    QCOMPARE(reparsed->headings()[i].m_startPos, positions[order[i]]);
    QCOMPARE(reparsed->headings()[i].m_title, titles[i]);
    QCOMPARE(reparsed->headings()[i].m_level, levels[i]);
  }
  document.undo();
  QCOMPARE(document.toPlainText(), original);
  QVERIFY(!document.isUndoAvailable());
  document.redo();
  QCOMPARE(document.toPlainText(), expected);
}

void TestMarkdownHeadingLink::setextParserTitleBoundaries_data() {
  QTest::addColumn<QString>("sourceHeading");
  QTest::addColumn<QString>("tail");
  QTest::addColumn<QString>("topic");
  QTest::addColumn<QString>("expectedHeading");
  const QString prose = QStringLiteral("\nBody Ω\n\n# End\n");
  QTest::newRow("prose-literal") << QStringLiteral("Old\n---") << prose
                                 << QStringLiteral("1. [item]")
                                 << QStringLiteral("1&#46; &#91;item&#93;\n---");
  QTest::newRow("prose-empty") << QStringLiteral("Old\n---") << prose << QString()
                               << QStringLiteral("##");
  QTest::newRow("atx-like-title") << QStringLiteral("##literal\n---") << prose
                                  << QStringLiteral("New") << QStringLiteral("New\n---");
  QTest::newRow("atx-like-title-empty")
      << QStringLiteral("##literal\n---") << prose << QString() << QStringLiteral("##");
  QTest::newRow("eof") << QStringLiteral("Old\n---") << QString() << QStringLiteral("New")
                       << QStringLiteral("New\n---");
  QTest::newRow("terminated-eof") << QStringLiteral("Old\n---") << QStringLiteral("\n")
                                  << QStringLiteral("New") << QStringLiteral("New\n---");
  QTest::newRow("whitespace-blank-line")
      << QStringLiteral("Old\n---") << QStringLiteral("\n \t\nBody Ω\n\n# End\n")
      << QStringLiteral("New") << QStringLiteral("New\n---");
}

void TestMarkdownHeadingLink::setextParserTitleBoundaries() {
  QFETCH(QString, sourceHeading);
  QFETCH(QString, tail);
  QFETCH(QString, topic);
  QFETCH(QString, expectedHeading);
  const QString preamble = QStringLiteral("Preamble Ω 😀\n\n");
  const QString original = preamble + sourceHeading + tail;
  const int options = vte::MarkdownDocument::viewerParseOptions(true, true);
  const auto parsed = vte::MarkdownDocument::parse(original.toUtf8(), options);
  QVERIFY(parsed && !parsed->headings().isEmpty());
  QVERIFY(parsed->headings()[0].m_isSetext);
  QVector<Heading> headings;
  for (const auto &item : parsed->headings()) {
    Heading info;
    info.level = item.m_level;
    info.startPos = item.m_startPos;
    info.endPos = item.m_endPos;
    headings.append(info);
  }
  QCOMPARE(headings[0].startPos, preamble.size());
  QTextDocument document(original);
  document.clearUndoRedoStacks();
  const auto result = OutlineSectionEditor::renameHeading(&document, headings, 0, topic,
                                                          original.size(), 0, -1, -1);
  QVERIFY(result.changed);
  const QString expected = preamble + expectedHeading + tail;
  QCOMPARE(document.toPlainText(), expected);
  QCOMPARE(result.cursorPosition, expected.size());
  QCOMPARE(result.cursorAnchor, 0);
  const auto reparsed = vte::MarkdownDocument::parse(expected.toUtf8(), options);
  QVERIFY(reparsed);
  QCOMPARE(reparsed->headings().size(), headings.size());
  QCOMPARE(reparsed->headings()[0].m_title, topic);
  QCOMPARE(reparsed->headings()[0].m_level, 2);
  QCOMPARE(reparsed->headings()[0].m_isSetext, !topic.isEmpty());
  for (int i = 0; i < headings.size(); ++i) {
    QCOMPARE(reparsed->headings()[i].m_startPos, result.headingPositions[i]);
  }
  document.undo();
  QCOMPARE(document.toPlainText(), original);
  QVERIFY(!document.isUndoAvailable());
  document.redo();
  QCOMPARE(document.toPlainText(), expected);
}

void TestMarkdownHeadingLink::setextParserOverlapsRemainRejected() {
  const QString original = QStringLiteral("Book\n====\n# Next\n");
  const auto parsed = vte::MarkdownDocument::parse(
      original.toUtf8(), vte::MarkdownDocument::viewerParseOptions(true, true));
  QVERIFY(parsed);
  QCOMPARE(parsed->headings().size(), 2);
  QVERIFY(parsed->headings()[0].m_isSetext);
  // The parser currently extends the first span into the following heading.
  // Do not turn that ambiguous snapshot into authorization by trimming overlaps.
  QVERIFY(parsed->headings()[0].m_endPos > parsed->headings()[1].m_startPos);
  QVector<Heading> headings;
  for (const auto &item : parsed->headings()) {
    Heading info;
    info.level = item.m_level;
    info.startPos = item.m_startPos;
    info.endPos = item.m_endPos;
    headings.append(info);
  }
  QTextDocument document(original);
  document.clearUndoRedoStacks();
  QVector<int> positions{123};
  QVERIFY(!OutlineSectionEditor::reorderHeadingBlock(&document, headings, 1, 0, 1, 0, 0, -1, -1,
                                                     OutlineSectionEditor::Syntax::Markdown,
                                                     &positions)
               .moved);
  QCOMPARE(positions, QVector<int>{123});
  QVERIFY(!OutlineSectionEditor::renameHeading(&document, headings, 0, QStringLiteral("Renamed"), 0,
                                               0, -1, -1)
               .changed);
  QVERIFY(!OutlineSectionEditor::insertHeading(&document, headings, 0, -1, 1,
                                               QStringLiteral("Inserted"), 0, 0, -1, -1)
               .changed);
  QCOMPARE(document.toPlainText(), original);
  QVERIFY(!document.isUndoAvailable());
}

void TestMarkdownHeadingLink::typstRenamePreservesCertifiedRanges() {
  const QString original =
      QStringLiteral("= *Old* /* keep */ #label(\"inside\")\n<label> // sibling\nbody\n= End\n");
  Heading first = heading(original, QStringLiteral("= *Old* /* keep */ #label(\"inside\")"), 1);
  first.bodyStartPos = 2;
  first.bodyEndPos = first.endPos;
  const int comment = original.indexOf(QStringLiteral("/* keep */"));
  const int label = original.indexOf(QStringLiteral("#label"));
  first.preservedBodyRanges = {{comment, comment + 10}, {label, first.endPos}};
  const auto last = heading(original, QStringLiteral("= End"), 1);
  QTextDocument document(original);
  document.clearUndoRedoStacks();
  const auto result = OutlineSectionEditor::renameHeading(
      &document, {first, last}, 0, QStringLiteral("Quote \" and \\"), original.size(), 0, -1, -1,
      OutlineSectionEditor::Syntax::Typst);
  QVERIFY(result.changed);
  QCOMPARE(document.toPlainText(),
           QStringLiteral("= #\"Quote \\\" and \\\\\"/* keep */#label(\"inside\")\n<label> // "
                          "sibling\nbody\n= End\n"));
  QCOMPARE(result.headingPositions[1], document.toPlainText().indexOf(QStringLiteral("= End")));
  document.undo();
  QCOMPARE(document.toPlainText(), original);
  QVERIFY(!document.isUndoAvailable());
  first.preservedBodyRanges[1].first = comment + 1;
  QVERIFY(!OutlineSectionEditor::renameHeading(&document, {first, last}, 0, {}, 0, 0, -1, -1,
                                               OutlineSectionEditor::Syntax::Typst)
               .changed);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMarkdownHeadingLink)
#include "test_markdown_heading_link.moc"
