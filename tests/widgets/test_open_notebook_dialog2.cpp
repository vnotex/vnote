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
//   4. In remote mode, a valid HTTPS URL + a non-existent destination derived
//      from a real parent dir enables the Open button.
//
// Per ADR-9 patterns used in adjacent dialog tests (test_notebooksyncinfodialog2):
// the dialog is instantiated directly with a ServiceLocator wired to a real
// vxcore context (test mode). No actual notebook is opened in these subtests.

#include <QApplication>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <widgets/dialogs/opennotebookdialog2.h>

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

  // 4. Remote mode: valid HTTPS URL + valid (non-existent) destination -> Open
  //    enabled.
  void testValidRemoteUrlEnablesOpenButton();

private:
  // Build a ServiceLocator with a real NotebookCoreService bound to the shared
  // vxcore context. Each subtest is isolated because no actual notebooks are
  // created (the dialog only constructs the controller; remote mode does not
  // touch it, and local mode is not exercised through accept() here).
  void buildServices(ServiceLocator &services, NotebookCoreService *&svc);

  VxCoreContextHandle m_ctx = nullptr;
};

void TestOpenNotebookDialog2::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create. Per
  // tests/AGENTS.md, this prevents tests from corrupting real user data.
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create("{}", &m_ctx), VXCORE_OK);
  QVERIFY(m_ctx != nullptr);
}

void TestOpenNotebookDialog2::cleanupTestCase() {
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
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
  auto *destEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteDestEdit"));
  auto *browseBtn = dialog.findChild<QPushButton *>(QStringLiteral("remoteDestBrowseButton"));
  QVERIFY2(urlEdit, "remoteUrlEdit must exist");
  QVERIFY2(patEdit, "remotePatEdit must exist");
  QVERIFY2(destEdit, "remoteDestEdit must exist");
  QVERIFY2(browseBtn, "remoteDestBrowseButton must exist");

  // PAT must use password echo mode (the password-mask contract).
  QCOMPARE(patEdit->echoMode(), QLineEdit::Password);
  // Destination must be read-only (user picks via Browse, not by typing).
  QVERIFY(destEdit->isReadOnly());

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
// Subtest 4: Valid URL + valid (non-existent) destination enables Open.
// =============================================================================
void TestOpenNotebookDialog2::testValidRemoteUrlEnablesOpenButton() {
  ServiceLocator services;
  NotebookCoreService *svc = nullptr;
  buildServices(services, svc);

  // Real parent directory the dialog can populate as parent + suggest leaf.
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
  auto *destEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteDestEdit"));
  QVERIFY(urlEdit);
  QVERIFY(destEdit);

  auto *box = dialog.getDialogButtonBox();
  QVERIFY(box);
  auto *openBtn = box->button(QDialogButtonBox::Open);
  QVERIFY(openBtn);

  // Set the URL first so the dialog will know the leaf name when we set the
  // parent (the dialog uses the URL leaf as the appended folder name).
  urlEdit->setText(QStringLiteral("https://github.com/vnotex/test-notebook.git"));
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  // Without a destination, Open must still be disabled.
  QVERIFY(!openBtn->isEnabled());

  // Bypass the QFileDialog by writing the destination directly. This is the
  // same observable state the dialog would land in after the user picks the
  // parent through Browse: an absolute path equal to <parent>/<leaf>. We
  // can't drive the modal QFileDialog from a test, so we set the field
  // directly and trust the textChanged-driven validation path.
  const QString computedDest = QDir::cleanPath(parentDir + QStringLiteral("/test-notebook"));
  QVERIFY2(!QFileInfo::exists(computedDest),
           "Test precondition: derived destination must not yet exist");

  // The dest line edit is read-only via the property; setText() still works.
  destEdit->setText(computedDest);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  QVERIFY2(openBtn->isEnabled(),
           "Open button must be enabled when URL is https://... and dest does not exist");

  // Sanity: introducing an existing destination should disable Open.
  destEdit->setText(parentDir); // parentDir itself definitely exists
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  QVERIFY2(!openBtn->isEnabled(),
           "Open button must be disabled when the destination folder already exists");

  delete svc;
}

} // namespace tests

QTEST_MAIN(tests::TestOpenNotebookDialog2)
#include "test_open_notebook_dialog2.moc"
