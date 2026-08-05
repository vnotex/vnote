#include <QtTest>

#include <QFile>
#include <QJsonObject>

#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>

#include <controllers/newnotecontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/snippetcoreservice.h>
#include <temp_dir_fixture.h>
#include <utils/pathutils.h>

using namespace vnotex;

namespace tests {

class TestNewNoteController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testCreateNoteWithCursorMark();
  void testCreateNoteWithoutCursorMark();
  void testCreateNoteOverrides();
  void testCreateNoteLiteralContentIsVerbatim();
  void testCreateNoteLiteralContentCleared();
  void testCreateQuickNoteExpandsBody();
  void testCreateQuickNoteSequencedOverrides();
  void testCreateQuickNoteMissingNotebook();

private:
  QString readNote(const NodeIdentifier &p_id) const;
  QByteArray readNoteBytes(const NodeIdentifier &p_id) const;

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  SnippetCoreService *m_snippetService = nullptr;
  ServiceLocator m_services;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestNewNoteController::initTestCase() {
  QVERIFY(m_tempDir.isValid());
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create("{}", &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_snippetService = new SnippetCoreService(m_context, this);
  m_services.registerService<NotebookCoreService>(m_notebookService);
  m_services.registerService<SnippetCoreService>(m_snippetService);

  // Create an empty-dir bundled notebook to host the notes.
  QString root = m_tempDir.createDir("nb_root");
  m_notebookId =
      m_notebookService->createNotebook(root, QStringLiteral("{}"), NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestNewNoteController::cleanupTestCase() {
  delete m_notebookService;
  m_notebookService = nullptr;
  delete m_snippetService;
  m_snippetService = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

QString TestNewNoteController::readNote(const NodeIdentifier &p_id) const {
  return QString::fromUtf8(readNoteBytes(p_id));
}

QByteArray TestNewNoteController::readNoteBytes(const NodeIdentifier &p_id) const {
  QJsonObject cfg = m_notebookService->getNotebookConfig(p_id.notebookId);
  QString root = cfg.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
  QString fullPath = PathUtils::concatenateFilePath(root, p_id.relativePath);
  QFile f(fullPath);
  if (!f.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  QByteArray content = f.readAll();
  f.close();
  return content;
}

void TestNewNoteController::testCreateNoteWithCursorMark() {
  NewNoteController controller(m_services);
  NewNoteInput input;
  input.notebookId = m_notebookId;
  input.name = QStringLiteral("with_mark.md");
  input.templateContent = QStringLiteral("Title\n@@body");

  NewNoteResult result = controller.createNote(input);
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QCOMPARE(result.cursorOffset, 6); // "Title\n" == 6 doc positions (LF collapsed).

  // The "@@" mark must be stripped from the written content.
  QCOMPARE(readNote(result.nodeId), QStringLiteral("Title\nbody"));
}

void TestNewNoteController::testCreateNoteWithoutCursorMark() {
  NewNoteController controller(m_services);
  NewNoteInput input;
  input.notebookId = m_notebookId;
  input.name = QStringLiteral("no_mark.md");
  input.templateContent = QStringLiteral("plain body");

  NewNoteResult result = controller.createNote(input);
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QCOMPARE(result.cursorOffset, -1);
  QCOMPARE(readNote(result.nodeId), QStringLiteral("plain body"));
}

void TestNewNoteController::testCreateNoteOverrides() {
  NewNoteController controller(m_services);
  NewNoteInput input;
  input.notebookId = m_notebookId;
  input.name = QStringLiteral("mynote.md");
  input.templateContent = QStringLiteral("file %note% base %no%@@");

  NewNoteResult result = controller.createNote(input);
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QCOMPARE(readNote(result.nodeId), QStringLiteral("file mynote.md base mynote"));
}

// Literal capture (macOS Services): the controller must persist the dialog text
// byte-for-byte. Nothing in it may be interpreted as a snippet, a magic symbol,
// a cursor mark or a selection mark, and no whitespace may be trimmed.
void TestNewNoteController::testCreateNoteLiteralContentIsVerbatim() {
  NewNoteController controller(m_services);

  // Already LF-normalized (QPlainTextEdit owns CRLF -> LF), with leading and
  // trailing spaces, a tab, a supplementary-plane character, and every marker
  // the template path would otherwise consume.
  const QString captured = QStringLiteral("  lead\tspaced\n%note% %no% @@ $$\n\U0001F4DD end  ");

  NewNoteInput input;
  input.notebookId = m_notebookId;
  input.name = QStringLiteral("literal.md");
  input.bodyMode = NewNoteBodyMode::LiteralContent;
  input.literalContent = captured;
  // A stale templateContent must be ignored in literal mode.
  input.templateContent = QStringLiteral("SHOULD NOT BE WRITTEN");

  NewNoteResult result = controller.createNote(input);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Raw bytes must match the controller input exactly.
  QCOMPARE(readNoteBytes(result.nodeId), captured.toUtf8());

  // Caret lands at the end of the captured text. Qt offsets are UTF-16 units,
  // so the supplementary character counts as two.
  QCOMPARE(result.cursorOffset, captured.size());
}

// A user who clears the Content field gets an empty note and a zero offset --
// not the "no content" (-1) sentinel of the template path.
void TestNewNoteController::testCreateNoteLiteralContentCleared() {
  NewNoteController controller(m_services);

  NewNoteInput input;
  input.notebookId = m_notebookId;
  input.name = QStringLiteral("literal_cleared.md");
  input.bodyMode = NewNoteBodyMode::LiteralContent;
  input.literalContent = QString();

  NewNoteResult result = controller.createNote(input);
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QCOMPARE(readNoteBytes(result.nodeId), QByteArray());
  QCOMPARE(result.cursorOffset, 0);
}

void TestNewNoteController::testCreateQuickNoteExpandsBody() {
  NewNoteController controller(m_services);
  QuickNoteInput input;
  input.notebookId = m_notebookId;
  input.noteNameScheme = QStringLiteral("quick.md");
  input.templateContent = QStringLiteral("head @@tail");

  NewNoteResult result = controller.createQuickNote(input);
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QCOMPARE(result.cursorOffset, 5); // "head " == 5.
  QVERIFY(result.nodeId.relativePath.endsWith(QStringLiteral(".md")));
  QCOMPARE(readNote(result.nodeId), QStringLiteral("head tail"));
}

void TestNewNoteController::testCreateQuickNoteSequencedOverrides() {
  NewNoteController controller(m_services);

  // First quick note with a fixed scheme name occupies "seq.md".
  QuickNoteInput first;
  first.notebookId = m_notebookId;
  first.noteNameScheme = QStringLiteral("seq.md");
  first.templateContent = QStringLiteral("%no%");
  NewNoteResult firstResult = controller.createQuickNote(first);
  QVERIFY2(firstResult.success, qPrintable(firstResult.errorMessage));
  QCOMPARE(readNote(firstResult.nodeId), QStringLiteral("seq"));

  // Second quick note with the SAME scheme must be sequenced to a new filename,
  // and the %note%/%no% overrides must derive from that FINAL sequenced name,
  // not the original scheme.
  QuickNoteInput second;
  second.notebookId = m_notebookId;
  second.noteNameScheme = QStringLiteral("seq.md");
  second.templateContent = QStringLiteral("%note% / %no%");
  NewNoteResult secondResult = controller.createQuickNote(second);
  QVERIFY2(secondResult.success, qPrintable(secondResult.errorMessage));

  const QString finalName = secondResult.nodeId.relativePath;
  QVERIFY(finalName != QStringLiteral("seq.md")); // sequenced away from the collision
  const QString base = finalName.left(finalName.lastIndexOf(QLatin1Char('.')));
  QCOMPARE(readNote(secondResult.nodeId), QStringLiteral("%1 / %2").arg(finalName, base));
}

void TestNewNoteController::testCreateQuickNoteMissingNotebook() {
  NewNoteController controller(m_services);
  QuickNoteInput input;
  input.notebookId = QString(); // no notebook → failure result
  input.noteNameScheme = QStringLiteral("orphan.md");
  input.templateContent = QStringLiteral("body");

  NewNoteResult result = controller.createQuickNote(input);
  QVERIFY(!result.success);
  QVERIFY(!result.errorMessage.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNewNoteController)
#include "test_newnotecontroller.moc"
