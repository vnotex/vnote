#include <QtTest>

#include <QJsonObject>
#include <QTextCursor>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/snippetcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>
#include <vtextedit/vtextedit.h>
#include <widgets/textviewwindowhelper.h>

using namespace vnotex;

namespace vnotex {

void ViewWindow2::findNext(const QString &, FindOptions) {}

void ViewWindow2::replace(const QString &, FindOptions, const QString &) {}

void ViewWindow2::replaceAll(const QString &, FindOptions, const QString &) {}

void ViewWindow2::onBufferAutoSaved(const QString &) {}

void ViewWindow2::onBufferModifiedChanged(const QString &) {}

void ViewWindow2::onAttachmentChanged(const QString &) {}

void ViewWindow2::refreshToolBarIcons() {}

QIcon ViewWindow2::getIcon() const { return {}; }

QString ViewWindow2::getName() const { return {}; }

QSharedPointer<OutlineProvider> ViewWindow2::getOutlineProvider() const { return {}; }

int ViewWindow2::getCursorPosition() const { return -1; }

int ViewWindow2::getScrollPosition() const { return -1; }

bool ViewWindow2::aboutToClose(bool) { return true; }

void ViewWindow2::applySnippet(const QString &) {}

void ViewWindow2::applySnippet() {}

void ViewWindow2::clearHighlights() {}

void ViewWindow2::applyFileOpenSettings(const FileOpenSettings &) {}

void ViewWindow2::handleEditorConfigChange() {}

void ViewWindow2::handleThemeChanged() {}

void ViewWindow2::handleFindTextChanged(const QString &, FindOptions) {}

void ViewWindow2::handleFindNext(const QStringList &, FindOptions) {}

void ViewWindow2::handleReplace(const QString &, FindOptions, const QString &) {}

void ViewWindow2::handleReplaceAll(const QString &, FindOptions, const QString &) {}

void ViewWindow2::handleFindAndReplaceWidgetClosed() {}

void ViewWindow2::handleFindAndReplaceWidgetOpened() {}

void ViewWindow2::applyReadableWidth() {}

void ViewWindow2::handleTypeAction(int) {}

void ViewWindow2::fetchWordCountInfo(
    const std::function<void(const WordCountInfo &)> &) const {}

void ViewWindow2::addAdditionalRightToolBarActions(QToolBar *) {}

void ViewWindow2::handlePrint() {}

void ViewWindow2::showFindAndReplaceWidget() {}

QString ViewWindow2::selectedText() const { return {}; }

QPoint ViewWindow2::getFloatingWidgetPosition() { return {}; }

bool ViewWindow2::eventFilter(QObject *, QEvent *) { return false; }

void ViewWindow2::keyPressEvent(QKeyEvent *) {}

void ViewWindow2::wheelEvent(QWheelEvent *) {}

void ViewWindow2::resizeEvent(QResizeEvent *) {}

} // namespace vnotex

namespace tests {

struct MockEditor {
  vte::VTextEdit *getTextEdit() { return m_textEdit; }

  bool isReadOnly() const { return m_readOnly; }

  void enterInsertModeIfApplicable() {}

  vte::VTextEdit *m_textEdit = nullptr;
  bool m_readOnly = false;
};

struct MockViewWindow {
  const Buffer2 &getBuffer() const { return m_buffer; }

  ServiceLocator &getServices() const { return *m_services; }

  void showMessage(const QString &p_msg) { m_lastMessage = p_msg; }

  QVariant showFloatingWidget(vnotex::FloatingWidget *) { return m_floatingResult; }

  MockEditor *m_editor = nullptr;
  Buffer2 m_buffer;
  ServiceLocator *m_services = nullptr;
  QString m_lastMessage;
  QVariant m_floatingResult;
};

class TestSnippetApply : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testOverrideGeneration();
  void testOverrideGenerationNoExtension();
  void testSymbolDetectionFound();
  void testSymbolDetectionNotFound();
  void testSymbolDetectionIncomplete();
  void testSymbolDetectionMultiple();
  void testSymbolDetectionAtBoundary();
  void testApplyByNameSuccess();
  void testApplyByNameNonexistent();
  void testApplyByNameReadOnly();

private:
  static QJsonObject makeValidSnippet(const QString &p_content = QStringLiteral("hello @@world"));

  MockViewWindow makeWindow(MockEditor &p_editor, const Buffer2 &p_buffer);

  Buffer2 openBuffer(const QString &p_relativePath);

  VxCoreContextHandle m_context = nullptr;
  SnippetCoreService *m_snippetService = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  HookManager *m_hookMgr = nullptr;
  BufferService *m_bufferService = nullptr;
  ServiceLocator m_services;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestSnippetApply::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_snippetService = new SnippetCoreService(m_context, this);
  m_notebookService = new NotebookCoreService(m_context, this);
  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::AutoSave, this);

  m_services.registerService<SnippetCoreService>(m_snippetService);

  const QString nbPath = m_tempDir.filePath(QStringLiteral("snippet_apply_test"));
  const QString configJson =
      QStringLiteral(R"({"name": "Snippet Apply Test", "description": "Test", "version": "1"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());

  const QString folderId =
      m_notebookService->createFolder(m_notebookId, QString(), QStringLiteral("notes"));
  QVERIFY(!folderId.isEmpty());

  const QString helloFileId =
      m_notebookService->createFile(m_notebookId, QStringLiteral("notes"), QStringLiteral("hello.md"));
  QVERIFY(!helloFileId.isEmpty());

  const QString readmeFileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("README"));
  QVERIFY(!readmeFileId.isEmpty());
}

void TestSnippetApply::cleanupTestCase() {
  m_bufferService = nullptr;
  m_hookMgr = nullptr;
  m_notebookService = nullptr;
  m_snippetService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestSnippetApply::cleanup() {
  m_snippetService->deleteSnippet(QStringLiteral("test_snip"));

  const QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufferVal : buffers) {
    const QString id = bufferVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

QJsonObject TestSnippetApply::makeValidSnippet(const QString &p_content) {
  QJsonObject json;
  json[QLatin1String("type")] = QStringLiteral("text");
  json[QLatin1String("description")] = QStringLiteral("test snippet");
  json[QLatin1String("content")] = p_content;
  json[QLatin1String("cursorMark")] = QStringLiteral("@@");
  json[QLatin1String("selectionMark")] = QStringLiteral("$$");
  json[QLatin1String("indentAsFirstLine")] = false;
  return json;
}

MockViewWindow TestSnippetApply::makeWindow(MockEditor &p_editor, const Buffer2 &p_buffer) {
  MockViewWindow win;
  win.m_editor = &p_editor;
  win.m_buffer = p_buffer;
  win.m_services = &m_services;
  return win;
}

Buffer2 TestSnippetApply::openBuffer(const QString &p_relativePath) {
  return m_bufferService->openBuffer(NodeIdentifier{m_notebookId, p_relativePath});
}

void TestSnippetApply::testOverrideGeneration() {
  Buffer2 buffer = openBuffer(QStringLiteral("notes/hello.md"));
  QVERIFY(buffer.isValid());

  vte::VTextEdit textEdit;
  MockEditor editor;
  editor.m_textEdit = &textEdit;
  auto win = makeWindow(editor, buffer);

  const auto result = TextViewWindowHelper::generateSnippetOverrides2(&win);
  QCOMPARE(result[QStringLiteral("note")].toString(), QStringLiteral("notes/hello.md"));
  QCOMPARE(result[QStringLiteral("no")].toString(), QStringLiteral("hello"));
}

void TestSnippetApply::testOverrideGenerationNoExtension() {
  Buffer2 buffer = openBuffer(QStringLiteral("README"));
  QVERIFY(buffer.isValid());

  vte::VTextEdit textEdit;
  MockEditor editor;
  editor.m_textEdit = &textEdit;
  auto win = makeWindow(editor, buffer);

  const auto result = TextViewWindowHelper::generateSnippetOverrides2(&win);
  QCOMPARE(result[QStringLiteral("note")].toString(), QStringLiteral("README"));
  QCOMPARE(result[QStringLiteral("no")].toString(), QStringLiteral("README"));
}

void TestSnippetApply::testSymbolDetectionFound() {
  Buffer2 buffer = openBuffer(QStringLiteral("notes/hello.md"));
  QVERIFY(buffer.isValid());

  const QString text = QStringLiteral("some %mysnippet% text");
  const int start = text.indexOf(QStringLiteral("%mysnippet%"));
  const int end = start + QStringLiteral("%mysnippet%").size();
  const int cursorPos = start + 4;

  vte::VTextEdit textEdit;
  textEdit.setPlainText(text);
  QTextCursor cursor = textEdit.textCursor();
  cursor.setPosition(cursorPos);
  textEdit.setTextCursor(cursor);

  MockEditor editor;
  editor.m_textEdit = &textEdit;
  auto win = makeWindow(editor, buffer);

  const auto result = TextViewWindowHelper::detectSnippetSymbol2(&win);
  QVERIFY(result.found);
  QCOMPARE(result.name, QStringLiteral("mysnippet"));
  QCOMPARE(result.start, start);
  QCOMPARE(result.end, end);
}

void TestSnippetApply::testSymbolDetectionNotFound() {
  Buffer2 buffer = openBuffer(QStringLiteral("notes/hello.md"));
  QVERIFY(buffer.isValid());

  vte::VTextEdit textEdit;
  textEdit.setPlainText(QStringLiteral("plain text only"));
  QTextCursor cursor = textEdit.textCursor();
  cursor.setPosition(2);
  textEdit.setTextCursor(cursor);

  MockEditor editor;
  editor.m_textEdit = &textEdit;
  auto win = makeWindow(editor, buffer);

  const auto result = TextViewWindowHelper::detectSnippetSymbol2(&win);
  QVERIFY(!result.found);
}

void TestSnippetApply::testSymbolDetectionIncomplete() {
  Buffer2 buffer = openBuffer(QStringLiteral("notes/hello.md"));
  QVERIFY(buffer.isValid());

  vte::VTextEdit textEdit;
  textEdit.setPlainText(QStringLiteral("some %incomplete text"));
  QTextCursor cursor = textEdit.textCursor();
  cursor.setPosition(8);
  textEdit.setTextCursor(cursor);

  MockEditor editor;
  editor.m_textEdit = &textEdit;
  auto win = makeWindow(editor, buffer);

  const auto result = TextViewWindowHelper::detectSnippetSymbol2(&win);
  QVERIFY(!result.found);
}

void TestSnippetApply::testSymbolDetectionMultiple() {
  Buffer2 buffer = openBuffer(QStringLiteral("notes/hello.md"));
  QVERIFY(buffer.isValid());

  const QString text = QStringLiteral("%first% and %second%");
  const int start = text.indexOf(QStringLiteral("%second%"));

  vte::VTextEdit textEdit;
  textEdit.setPlainText(text);
  QTextCursor cursor = textEdit.textCursor();
  cursor.setPosition(start + 2);
  textEdit.setTextCursor(cursor);

  MockEditor editor;
  editor.m_textEdit = &textEdit;
  auto win = makeWindow(editor, buffer);

  const auto result = TextViewWindowHelper::detectSnippetSymbol2(&win);
  QVERIFY(result.found);
  QCOMPARE(result.name, QStringLiteral("second"));
  QCOMPARE(result.start, start);
}

void TestSnippetApply::testSymbolDetectionAtBoundary() {
  Buffer2 buffer = openBuffer(QStringLiteral("notes/hello.md"));
  QVERIFY(buffer.isValid());

  const QString text = QStringLiteral("x %name% y");
  const int start = text.indexOf(QStringLiteral("%name%"));

  vte::VTextEdit textEdit;
  textEdit.setPlainText(text);
  QTextCursor cursor = textEdit.textCursor();
  cursor.setPosition(start);
  textEdit.setTextCursor(cursor);

  MockEditor editor;
  editor.m_textEdit = &textEdit;
  auto win = makeWindow(editor, buffer);

  const auto result = TextViewWindowHelper::detectSnippetSymbol2(&win);
  QVERIFY(result.found);
  QCOMPARE(result.name, QStringLiteral("name"));
  QCOMPARE(result.start, start);
}

void TestSnippetApply::testApplyByNameSuccess() {
  QVERIFY(m_snippetService->createSnippet(QStringLiteral("test_snip"), makeValidSnippet()));

  Buffer2 buffer = openBuffer(QStringLiteral("notes/hello.md"));
  QVERIFY(buffer.isValid());

  vte::VTextEdit textEdit;
  textEdit.setPlainText(QString());
  QTextCursor cursor = textEdit.textCursor();
  cursor.setPosition(0);
  textEdit.setTextCursor(cursor);

  MockEditor editor;
  editor.m_textEdit = &textEdit;
  auto win = makeWindow(editor, buffer);

  TextViewWindowHelper::applySnippetByName2(&win, QStringLiteral("test_snip"));

  QCOMPARE(textEdit.toPlainText(), QStringLiteral("hello world"));
  QCOMPARE(textEdit.textCursor().position(), 6);
  QVERIFY(win.m_lastMessage.contains(QStringLiteral("Snippet applied")));
}

void TestSnippetApply::testApplyByNameNonexistent() {
  Buffer2 buffer = openBuffer(QStringLiteral("notes/hello.md"));
  QVERIFY(buffer.isValid());

  vte::VTextEdit textEdit;
  textEdit.setPlainText(QStringLiteral("unchanged"));

  MockEditor editor;
  editor.m_textEdit = &textEdit;
  auto win = makeWindow(editor, buffer);

  TextViewWindowHelper::applySnippetByName2(&win, QStringLiteral("does_not_exist"));

  QCOMPARE(textEdit.toPlainText(), QStringLiteral("unchanged"));
  QVERIFY(win.m_lastMessage.contains(QStringLiteral("not found"), Qt::CaseInsensitive));
}

void TestSnippetApply::testApplyByNameReadOnly() {
  QVERIFY(m_snippetService->createSnippet(QStringLiteral("test_snip"), makeValidSnippet()));

  Buffer2 buffer = openBuffer(QStringLiteral("notes/hello.md"));
  QVERIFY(buffer.isValid());

  vte::VTextEdit textEdit;
  textEdit.setPlainText(QStringLiteral("readonly"));

  MockEditor editor;
  editor.m_textEdit = &textEdit;
  editor.m_readOnly = true;
  auto win = makeWindow(editor, buffer);

  TextViewWindowHelper::applySnippetByName2(&win, QStringLiteral("test_snip"));

  QCOMPARE(textEdit.toPlainText(), QStringLiteral("readonly"));
  QVERIFY(win.m_lastMessage.isEmpty());
}

} // namespace tests

QTEST_MAIN(tests::TestSnippetApply)
#include "test_snippet_apply.moc"
