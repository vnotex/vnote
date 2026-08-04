// Tests for OpenNotebookDialog2 (T24 — Open Notebook two-mode dialog).
//
// Verifies the UI-level behavior of the dialog without exercising the actual
// clone path (T22's job). Specifically:
//   1. Default mode is Local Folder; remote-mode page is not the active stack
//      child.
//   2. Clicking the Remote URL radio swaps the stacked widget to the remote
//      page; local page is no longer current.
//   3. In remote mode, an invalid URL scheme (ssh://...) keeps the Open
//      button disabled.
//   4. In remote mode, a valid HTTPS URL + a non-existent or existing-empty
//      destination enables the Open button; an existing non-empty path
//      disables it (refine-open-notebook-dialog: relaxed dest contract).
//
// This file also hosts the NewNoteDialog2 constructor-options coverage (macOS
// Services note capture) because it already owns a fully wired ServiceLocator +
// QApplication dialog harness, plus the platform-independent
// Application::dispatchCaptureText seam the native provider delegates to.
//
// Per ADR-9 patterns used in adjacent dialog tests (test_notebooksyncinfodialog2):
// the dialog is instantiated directly with a ServiceLocator wired to a real
// vxcore context (test mode). No actual notebook is opened in these subtests.

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QtTest>

#include <application.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/snippetcoreservice.h>
#include <core/services/templateservice.h>
#include <temp_dir_fixture.h>
#include <widgets/dialogs/newnotedialog2.h>
#include <widgets/dialogs/notetemplateselector.h>
#include <widgets/dialogs/opennotebookdialog2.h>
#include <widgets/lineeditwithsnippet.h>
#include <widgets/locationinputwithbrowsebutton.h>

#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestOpenNotebookDialog2 : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // 1. Dialog opens, default mode is Local Folder, remote-mode page is NOT
  //    the active stack child.
  void testDefaultModeIsLocal();

  // 2. Click "Remote URL" radio -> stack swaps to remote page; local page is
  //    no longer current.
  void testSwitchToRemoteMode();

  // 3. Remote mode: paste invalid URL scheme (ssh://...) -> Open disabled.
  void testInvalidRemoteUrlSchemeKeepsOpenDisabled();

  // 4. Remote mode: valid HTTPS URL + valid destination (non-existing OR
  //    existing-empty) -> Open enabled. Non-empty existing dest -> disabled.
  void testValidRemoteUrlEnablesOpenButton();

  // --- NewNoteDialog2 constructor options (macOS Services note capture) ---
  void testNewNoteDefaultOptionsKeepTemplate();
  void testNewNoteLiteralOptionsShowEditableContent();
  void testNewNoteLiteralAcceptPersistsVerbatim();
  void testNewNoteLiteralClearedAcceptPersistsEmpty();
  void testNewNoteLiteralDoesNotTouchLastTemplate();

  // --- Application::dispatchCaptureText seam ---
  void testDispatchRejectsMissingAndWhitespaceOnly();
  void testDispatchAcceptsTextVerbatim();
  void testDispatchWithoutCallbackIsRefused();

private:
  // Build a ServiceLocator with a real NotebookCoreService bound to the shared
  // vxcore context. Each subtest is isolated because no actual notebooks are
  // created (the dialog only constructs the controller; remote mode does not
  // touch it, and local mode is not exercised through accept() here).
  void buildServices(ServiceLocator &services, NotebookCoreService *&svc);

  // Parent folder (notebook root) the NewNoteDialog2 subtests create into.
  NodeIdentifier newNoteParentId() const;

  // Raw on-disk bytes of a created note, so persistence is asserted rather than
  // the widget's own displayed text.
  QByteArray readNoteBytes(const NodeIdentifier &p_id) const;

  // Drive a constructed dialog to accept with the given note name.
  void acceptNewNoteDialog(NewNoteDialog2 &p_dialog, const QString &p_name);

  VxCoreContextHandle m_ctx = nullptr;

  // Fully wired service graph for the NewNoteDialog2 subtests. Owned here so a
  // dialog can actually be accepted (which needs config + notebook services).
  ServiceLocator m_newNoteServices;
  ConfigCoreService *m_configCore = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  NotebookCoreService *m_newNoteNotebookSvc = nullptr;
  FileTypeCoreService *m_fileTypeSvc = nullptr;
  SnippetCoreService *m_snippetSvc = nullptr;
  TemplateService *m_templateSvc = nullptr;
  TempDirFixture m_newNoteTempDir;
  QString m_newNoteNotebookId;
};

void TestOpenNotebookDialog2::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create. Per
  // tests/AGENTS.md, this prevents tests from corrupting real user data.
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create("{}", &m_ctx), VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  // Service graph for the NewNoteDialog2 subtests.
  QVERIFY(m_newNoteTempDir.isValid());

  m_configCore = new ConfigCoreService(m_ctx);
  m_configMgr = new ConfigMgr2(m_configCore);
  // Required so getWidgetConfig() works inside the dialog; skipping it crashes
  // that path (same rationale as test_node_explorer_reload_expansion).
  m_configMgr->init();

  m_newNoteNotebookSvc = new NotebookCoreService(m_ctx);
  m_fileTypeSvc = new FileTypeCoreService(m_ctx, QStringLiteral("en_US"));
  m_snippetSvc = new SnippetCoreService(m_ctx);
  m_templateSvc = new TemplateService(m_configMgr);

  m_newNoteServices.registerService<ConfigCoreService>(m_configCore);
  m_newNoteServices.registerService<ConfigMgr2>(m_configMgr);
  m_newNoteServices.registerService<NotebookCoreService>(m_newNoteNotebookSvc);
  m_newNoteServices.registerService<FileTypeCoreService>(m_fileTypeSvc);
  m_newNoteServices.registerService<SnippetCoreService>(m_snippetSvc);
  m_newNoteServices.registerService<TemplateService>(m_templateSvc);

  const QString root = m_newNoteTempDir.createDir("capture_nb");
  m_newNoteNotebookId =
      m_newNoteNotebookSvc->createNotebook(root, QStringLiteral("{}"), NotebookType::Bundled);
  QVERIFY(!m_newNoteNotebookId.isEmpty());
}

void TestOpenNotebookDialog2::cleanupTestCase() {
  delete m_templateSvc;
  m_templateSvc = nullptr;
  delete m_snippetSvc;
  m_snippetSvc = nullptr;
  delete m_fileTypeSvc;
  m_fileTypeSvc = nullptr;
  delete m_newNoteNotebookSvc;
  m_newNoteNotebookSvc = nullptr;
  delete m_configMgr;
  m_configMgr = nullptr;
  delete m_configCore;
  m_configCore = nullptr;

  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

NodeIdentifier TestOpenNotebookDialog2::newNoteParentId() const {
  NodeIdentifier id;
  id.notebookId = m_newNoteNotebookId;
  id.relativePath = QString();
  return id;
}

QByteArray TestOpenNotebookDialog2::readNoteBytes(const NodeIdentifier &p_id) const {
  const QJsonObject cfg = m_newNoteNotebookSvc->getNotebookConfig(p_id.notebookId);
  const QString root = cfg.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
  QFile f(QDir(root).filePath(p_id.relativePath));
  if (!f.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  const QByteArray content = f.readAll();
  f.close();
  return content;
}

void TestOpenNotebookDialog2::acceptNewNoteDialog(NewNoteDialog2 &p_dialog, const QString &p_name) {
  auto *nameEdit = p_dialog.findChild<LineEditWithSnippet *>();
  QVERIFY(nameEdit);
  nameEdit->setText(p_name);

  auto *box = p_dialog.getDialogButtonBox();
  QVERIFY(box);
  auto *okBtn = box->button(QDialogButtonBox::Ok);
  QVERIFY(okBtn);
  okBtn->click();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  QCOMPARE(p_dialog.result(), static_cast<int>(QDialog::Accepted));
}

void TestOpenNotebookDialog2::buildServices(ServiceLocator &services, NotebookCoreService *&svc) {
  svc = new NotebookCoreService(m_ctx);
  services.registerService<NotebookCoreService>(svc);
}

// =============================================================================
// Subtest 1: Default mode is Local Folder.
// =============================================================================
void TestOpenNotebookDialog2::testDefaultModeIsLocal() {
  ServiceLocator services;
  NotebookCoreService *svc = nullptr;
  buildServices(services, svc);

  OpenNotebookDialog2 dialog(services);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  auto *localRadio = dialog.findChild<QRadioButton *>(QStringLiteral("localModeRadio"));
  auto *remoteRadio = dialog.findChild<QRadioButton *>(QStringLiteral("remoteModeRadio"));
  auto *stack = dialog.findChild<QStackedWidget *>(QStringLiteral("modeStack"));
  QVERIFY2(localRadio, "Local Folder radio button must exist with object name 'localModeRadio'");
  QVERIFY2(remoteRadio, "Remote URL radio button must exist with object name 'remoteModeRadio'");
  QVERIFY2(stack, "Mode stack must exist with object name 'modeStack'");

  // Default selection.
  QVERIFY2(localRadio->isChecked(), "Local Folder radio should be checked by default");
  QVERIFY2(!remoteRadio->isChecked(), "Remote URL radio should NOT be checked by default");
  QCOMPARE(dialog.currentMode(), OpenNotebookDialog2::LocalMode);

  // Stack shows the local page (index 0). The remote-mode page widget exists
  // but is not the current page, so its children are not the user-facing UI.
  QCOMPARE(stack->currentIndex(), 0);

  auto *localRootInput = dialog.findChild<QObject *>(QStringLiteral("localRootInput"));
  QVERIFY2(localRootInput, "Local root input must exist with object name 'localRootInput'");

  // Remote-mode widgets exist (constructed at setup) but their stack page is
  // not current.
  auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
  QVERIFY2(urlEdit, "Remote URL line edit must exist with object name 'remoteUrlEdit'");
  QVERIFY2(stack->currentWidget() != urlEdit->parentWidget(),
           "Remote page must NOT be the current stack page in default mode");

  // Open button starts disabled (no input yet).
  auto *box = dialog.getDialogButtonBox();
  QVERIFY(box);
  auto *openBtn = box->button(QDialogButtonBox::Open);
  QVERIFY2(openBtn, "Open button must exist");
  QVERIFY2(!openBtn->isEnabled(), "Open button must be disabled before any input");

  delete svc;
}

// =============================================================================
// Subtest 2: Switch to Remote mode flips the active stack page.
// =============================================================================
void TestOpenNotebookDialog2::testSwitchToRemoteMode() {
  ServiceLocator services;
  NotebookCoreService *svc = nullptr;
  buildServices(services, svc);

  OpenNotebookDialog2 dialog(services);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  auto *remoteRadio = dialog.findChild<QRadioButton *>(QStringLiteral("remoteModeRadio"));
  auto *stack = dialog.findChild<QStackedWidget *>(QStringLiteral("modeStack"));
  QVERIFY(remoteRadio);
  QVERIFY(stack);

  // Sanity: starts at index 0 (Local).
  QCOMPARE(stack->currentIndex(), 0);

  // Click Remote -> mode switches.
  remoteRadio->setChecked(true);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  QCOMPARE(stack->currentIndex(), 1);
  QCOMPARE(dialog.currentMode(), OpenNotebookDialog2::RemoteMode);

  auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
  auto *patEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remotePatEdit"));
  auto *destInput =
      dialog.findChild<LocationInputWithBrowseButton *>(QStringLiteral("remoteDestInput"));
  QVERIFY2(urlEdit, "remoteUrlEdit must exist");
  QVERIFY2(patEdit, "remotePatEdit must exist");
  QVERIFY2(destInput, "remoteDestInput (LocationInputWithBrowseButton) must exist");

  // PAT must use password echo mode (the password-mask contract).
  QCOMPARE(patEdit->echoMode(), QLineEdit::Password);

  // Switching back to Local should restore index 0.
  auto *localRadio = dialog.findChild<QRadioButton *>(QStringLiteral("localModeRadio"));
  QVERIFY(localRadio);
  localRadio->setChecked(true);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  QCOMPARE(stack->currentIndex(), 0);
  QCOMPARE(dialog.currentMode(), OpenNotebookDialog2::LocalMode);

  delete svc;
}

// =============================================================================
// Subtest 3: Invalid URL scheme keeps Open disabled.
// =============================================================================
void TestOpenNotebookDialog2::testInvalidRemoteUrlSchemeKeepsOpenDisabled() {
  ServiceLocator services;
  NotebookCoreService *svc = nullptr;
  buildServices(services, svc);

  OpenNotebookDialog2 dialog(services);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  // Switch to remote.
  auto *remoteRadio = dialog.findChild<QRadioButton *>(QStringLiteral("remoteModeRadio"));
  QVERIFY(remoteRadio);
  remoteRadio->setChecked(true);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
  QVERIFY(urlEdit);

  auto *box = dialog.getDialogButtonBox();
  QVERIFY(box);
  auto *openBtn = box->button(QDialogButtonBox::Open);
  QVERIFY(openBtn);

  // ssh:// is explicitly NOT allowed (we accept only https:// and file:///).
  urlEdit->setText(QStringLiteral("ssh://example.com/repo.git"));
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  QVERIFY2(!openBtn->isEnabled(),
           "Open button must stay disabled when URL scheme is not https or file://");

  // Other invalid schemes that previously surfaced in user reports:
  urlEdit->setText(QStringLiteral("git://example.com/repo.git"));
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  QVERIFY(!openBtn->isEnabled());

  urlEdit->setText(QStringLiteral("http://example.com/repo.git")); // http not https
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  QVERIFY(!openBtn->isEnabled());

  delete svc;
}

// =============================================================================
// Subtest 4: Valid URL + valid destination enables Open.
//
// Post refine-open-notebook-dialog the dest contract is "non-existing OR
// existing-empty", so this subtest covers three branches:
//   a) non-existing path with a writable parent       -> Open ENABLED
//   b) existing empty directory                       -> Open ENABLED
//   c) existing non-empty directory                   -> Open DISABLED
//
// The user-typed/browse value lands in the LocationInputWithBrowseButton's
// internal QLineEdit; we drive it via setText() to bypass the modal QFileDialog
// (same observable state).
// =============================================================================
void TestOpenNotebookDialog2::testValidRemoteUrlEnablesOpenButton() {
  ServiceLocator services;
  NotebookCoreService *svc = nullptr;
  buildServices(services, svc);

  // Real parent directory the dialog can validate against.
  TempDirFixture parentTemp;
  QVERIFY(parentTemp.isValid());
  const QString parentDir = parentTemp.path();
  QVERIFY(QFileInfo::exists(parentDir));

  OpenNotebookDialog2 dialog(services);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  // Switch to remote.
  auto *remoteRadio = dialog.findChild<QRadioButton *>(QStringLiteral("remoteModeRadio"));
  QVERIFY(remoteRadio);
  remoteRadio->setChecked(true);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
  auto *destInput =
      dialog.findChild<LocationInputWithBrowseButton *>(QStringLiteral("remoteDestInput"));
  QVERIFY(urlEdit);
  QVERIFY(destInput);

  auto *box = dialog.getDialogButtonBox();
  QVERIFY(box);
  auto *openBtn = box->button(QDialogButtonBox::Open);
  QVERIFY(openBtn);

  urlEdit->setText(QStringLiteral("https://github.com/vnotex/test-notebook.git"));
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  // Without a destination, Open must still be disabled.
  QVERIFY(!openBtn->isEnabled());

  // (a) Non-existing path with a writable parent: Open enabled.
  const QString nonExistingDest = QDir::cleanPath(parentDir + QStringLiteral("/test-notebook"));
  QVERIFY2(!QFileInfo::exists(nonExistingDest),
           "Test precondition: derived destination must not yet exist");
  destInput->setText(nonExistingDest);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  QVERIFY2(openBtn->isEnabled(),
           "Open must be enabled when URL is https://... and dest does not exist");

  // (b) Existing EMPTY directory: still enabled (relaxed contract).
  const QString existingEmptyDest = QDir::cleanPath(parentDir + QStringLiteral("/empty-dest"));
  QVERIFY(QDir().mkpath(existingEmptyDest));
  QVERIFY(QFileInfo(existingEmptyDest).isDir());
  destInput->setText(existingEmptyDest);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  QVERIFY2(openBtn->isEnabled(), "Open must be enabled when dest is an existing empty directory");

  // (c) Existing NON-EMPTY directory: disabled. Seed a sentinel file inside.
  const QString existingNonEmptyDest =
      QDir::cleanPath(parentDir + QStringLiteral("/nonempty-dest"));
  QVERIFY(QDir().mkpath(existingNonEmptyDest));
  {
    QFile sentinel(existingNonEmptyDest + QStringLiteral("/sentinel.txt"));
    QVERIFY(sentinel.open(QIODevice::WriteOnly));
    sentinel.write("x");
    sentinel.close();
  }
  destInput->setText(existingNonEmptyDest);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  QVERIFY2(!openBtn->isEnabled(),
           "Open must be DISABLED when dest is an existing non-empty directory");

  delete svc;
}

// =============================================================================
// NewNoteDialog2 constructor options.
//
// The macOS "Create Note in VNote" Service opens the ordinary New Note dialog
// in a literal-content mode. The mode is fixed at construction (there are no
// setters), so these subtests assert per-mode control existence and content
// handling. Persistence itself is the controller's job and is covered by
// tests/controllers/test_newnotecontroller.cpp.
// =============================================================================

// Default options must reproduce today's New Note dialog exactly: Template
// selector present, no Content field.
void TestOpenNotebookDialog2::testNewNoteDefaultOptionsKeepTemplate() {
  NewNoteDialog2 dialog(m_newNoteServices, newNoteParentId());
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  QVERIFY2(dialog.findChild<NoteTemplateSelector *>() != nullptr,
           "Default options must still show the template selector");
  QVERIFY2(dialog.findChild<QPlainTextEdit *>(QStringLiteral("newNoteContentEdit")) == nullptr,
           "Default options must NOT show a Content field");
}

// Capture options must expose an editable, named Content field seeded verbatim
// (no trimming) and must NOT construct the template selector.
void TestOpenNotebookDialog2::testNewNoteLiteralOptionsShowEditableContent() {
  // Leading/trailing whitespace, a tab, and snippet markers that the template
  // path would otherwise consume.
  const QString captured = QStringLiteral("  lead\tkeep %note% @@ $$ trail  ");

  NewNoteDialog2::Options options;
  options.m_bodyMode = NewNoteDialog2::BodyMode::LiteralContent;
  options.m_initialContent = captured;

  NewNoteDialog2 dialog(m_newNoteServices, newNoteParentId(), options);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  auto *contentEdit = dialog.findChild<QPlainTextEdit *>(QStringLiteral("newNoteContentEdit"));
  QVERIFY2(contentEdit, "Content field must exist with object name 'newNoteContentEdit'");
  QVERIFY2(contentEdit->isEnabled(), "Content field must be enabled");
  QVERIFY2(!contentEdit->isReadOnly(), "Content field must be editable");
  QCOMPARE(contentEdit->toPlainText(), captured);

  QVERIFY2(dialog.findChild<NoteTemplateSelector *>() == nullptr,
           "Capture options must NOT construct the template selector");
}

// End-to-end through the REAL dialog -> NewNoteInput -> NewNoteController ->
// file path. Asserts the persisted bytes, not the widget's displayed text, so
// an accidental trim / template-mode forwarding / snippet expansion would fail
// here.
//
// Exactly one mutation is permitted: CRLF and lone CR collapse to LF. In
// particular U+00A0 must survive, which QPlainTextEdit::toPlainText() would
// silently rewrite to a plain space.
void TestOpenNotebookDialog2::testNewNoteLiteralAcceptPersistsVerbatim() {
  const QString captured =
      QStringLiteral("  a\r\nb\rc\n\u00A0nbsp\t%note% @@ $$ \U0001F4DD tail  ");
  const QString expected = QStringLiteral("  a\nb\nc\n\u00A0nbsp\t%note% @@ $$ \U0001F4DD tail  ");

  NewNoteDialog2::Options options;
  options.m_bodyMode = NewNoteDialog2::BodyMode::LiteralContent;
  options.m_initialContent = captured;

  NewNoteDialog2 dialog(m_newNoteServices, newNoteParentId(), options);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  acceptNewNoteDialog(dialog, QStringLiteral("capture_verbatim.md"));

  const NodeIdentifier newId = dialog.getNewNodeId();
  QVERIFY(newId.isValid());
  QCOMPARE(readNoteBytes(newId), expected.toUtf8());

  // Caret lands at the end of the captured content (UTF-16 units).
  QCOMPARE(dialog.getNewCursorOffset(), expected.size());
}

// The user may clear the captured text entirely; the accepted note is then
// empty with an offset of zero (NOT the template path's -1 "no content"
// sentinel, and NOT the original content).
void TestOpenNotebookDialog2::testNewNoteLiteralClearedAcceptPersistsEmpty() {
  NewNoteDialog2::Options options;
  options.m_bodyMode = NewNoteDialog2::BodyMode::LiteralContent;
  options.m_initialContent = QStringLiteral("discard me");

  NewNoteDialog2 dialog(m_newNoteServices, newNoteParentId(), options);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  auto *contentEdit = dialog.findChild<QPlainTextEdit *>(QStringLiteral("newNoteContentEdit"));
  QVERIFY(contentEdit);
  contentEdit->clear();

  acceptNewNoteDialog(dialog, QStringLiteral("capture_cleared.md"));

  const NodeIdentifier newId = dialog.getNewNodeId();
  QVERIFY(newId.isValid());
  QCOMPARE(readNoteBytes(newId), QByteArray());
  QCOMPARE(dialog.getNewCursorOffset(), 0);
}

// A capture dialog must neither READ nor OVERWRITE the shared static
// last-template state that ordinary New Note dialogs restore and save.
void TestOpenNotebookDialog2::testNewNoteLiteralDoesNotTouchLastTemplate() {
  // Seed one template so the selector has something to remember.
  QVERIFY(m_templateSvc->ensureTemplateFolder());
  const QString templateName = QStringLiteral("capture_probe.md");
  {
    QFile f(QDir(m_templateSvc->getTemplateFolder()).filePath(templateName));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("template body");
    f.close();
  }

  // (a) Ordinary dialog selects the template and is accepted -> remembered.
  {
    NewNoteDialog2 dialog(m_newNoteServices, newNoteParentId());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    auto *selector = dialog.findChild<NoteTemplateSelector *>();
    QVERIFY(selector);
    QVERIFY2(selector->setCurrentTemplate(templateName), "Seeded template must be selectable");
    acceptNewNoteDialog(dialog, QStringLiteral("last_tpl_a.md"));
  }

  // (b) A capture dialog runs in between and is accepted.
  {
    NewNoteDialog2::Options options;
    options.m_bodyMode = NewNoteDialog2::BodyMode::LiteralContent;
    options.m_initialContent = QStringLiteral("captured body");

    NewNoteDialog2 dialog(m_newNoteServices, newNoteParentId(), options);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    acceptNewNoteDialog(dialog, QStringLiteral("last_tpl_b.md"));
  }

  // (c) The next ordinary dialog must still restore the template from (a).
  {
    NewNoteDialog2 dialog(m_newNoteServices, newNoteParentId());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    auto *selector = dialog.findChild<NoteTemplateSelector *>();
    QVERIFY(selector);
    QCOMPARE(selector->getCurrentTemplate(), templateName);
  }
}

// =============================================================================
// Application::dispatchCaptureText
//
// The native Objective-C provider does the AppKit pasteboard read and then
// delegates every decision here, so this seam IS the Service contract.
// =============================================================================

void TestOpenNotebookDialog2::testDispatchRejectsMissingAndWhitespaceOnly() {
  const QStringList rejected = {QString(),                   // no pasteboard string at all
                                QString::fromLatin1(""),     // empty
                                QStringLiteral("   "),       // spaces
                                QStringLiteral("\t\n \r\n"), // whitespace mix
                                QStringLiteral("\n")};

  for (const QString &input : rejected) {
    int calls = 0;
    QString error = QStringLiteral("pre-existing");
    const bool accepted =
        Application::dispatchCaptureText(input, error, [&calls](const QString &) { ++calls; });

    QVERIFY2(!accepted, "whitespace-only or missing input must be rejected");
    QVERIFY2(!error.isEmpty(), "a rejection must report a non-empty error");
    QCOMPARE(calls, 0);
  }
}

void TestOpenNotebookDialog2::testDispatchAcceptsTextVerbatim() {
  // Surrounding whitespace and Unicode must survive untouched: validation may
  // inspect trimmed(), but it must never trim what it forwards.
  const QString captured = QStringLiteral("  \u4E2D\u6587 \U0001F4DD tail\t");

  int calls = 0;
  QString seen;
  QString error = QStringLiteral("pre-existing");

  const bool accepted =
      Application::dispatchCaptureText(captured, error, [&](const QString &p_text) {
        ++calls;
        seen = p_text;
      });

  QVERIFY(accepted);
  QVERIFY2(error.isEmpty(), "an accepted request must clear the error");
  QCOMPARE(calls, 1);
  QCOMPARE(seen, captured);
}

// A true return must mean the request was actually dispatched, so an absent
// callback is reported as a failure rather than a silent success.
void TestOpenNotebookDialog2::testDispatchWithoutCallbackIsRefused() {
  QString error;
  const bool accepted =
      Application::dispatchCaptureText(QStringLiteral("some text"), error, nullptr);

  QVERIFY(!accepted);
  QVERIFY(!error.isEmpty());
}

} // namespace tests

QTEST_MAIN(tests::TestOpenNotebookDialog2)
#include "test_open_notebook_dialog2.moc"
