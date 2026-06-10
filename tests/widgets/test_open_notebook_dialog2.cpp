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
// Per ADR-9 patterns used in adjacent dialog tests (test_notebooksyncinfodialog2):
// the dialog is instantiated directly with a ServiceLocator wired to a real
// vxcore context (test mode). No actual notebook is opened in these subtests.

#include <QApplication>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
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
#include <widgets/locationinputwithbrowsebutton.h>

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

} // namespace tests

QTEST_MAIN(tests::TestOpenNotebookDialog2)
#include "test_open_notebook_dialog2.moc"
