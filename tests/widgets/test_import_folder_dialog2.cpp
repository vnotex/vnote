// ImportFolderDialog2 two-mode UI test (import-shared-folder-bundle).
//
// The dialog gained an "External folder" / "Shared folder from VNote" mode
// selector over a QStackedWidget, modeled on OpenNotebookDialog2. These cases
// cover the three properties that are easy to get wrong when a dialog grows a
// second page:
//
//   * switching modes swaps the stack AND clears the banner left by the other
//     page, so a stale error cannot be attributed to the new mode;
//   * a signal from the HIDDEN page must not move the OK button — every field
//     handler early-returns when the mode does not match;
//   * the bundle radio is disabled with an explanatory tooltip when the
//     destination notebook cannot accept a bundle (raw or read-only).
//
// NOT GUILESS: QApplication is required for QDialog construction, QStackedWidget
// layout and QRadioButton click semantics.

#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QtTest>

#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

#include <widgets/dialogs/importfolderdialog2.h>

using namespace vnotex;

namespace tests {

class TestImportFolderDialog2 : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void testStartsInExternalModeWithOkDisabled();
  void testModeSwitchSwapsTheStackAndClearsTheBanner();
  void testHiddenPageSignalsDoNotEnableOk();
  void testBundleRadioDisabledForARawNotebook();
  void testBundleRadioDisabledForAReadOnlyNotebook();
  void testInvalidBundlePathIsRejectedWithAMessage();

private:
  NodeIdentifier rootOf(const QString &p_notebookId) const;

  // The dialog exposes no getters for these, so read them off the widgets the
  // base Dialog builds: the Ok button in the button box, and the banner's
  // plain-text edit.
  static bool okEnabled(ImportFolderDialog2 &p_dialog);
  static QString bannerText(ImportFolderDialog2 &p_dialog);

  TempDirFixture *m_tempDir = nullptr;
  VxCoreContextHandle m_context = nullptr;
  ServiceLocator m_services;
  NotebookCoreService *m_notebooks = nullptr;

  QString m_notebookId;
  QString m_notebookPath;
};

void TestImportFolderDialog2::init() {
  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());

  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context);

  m_notebooks = new NotebookCoreService(m_context, this);
  m_services.registerService<NotebookCoreService>(m_notebooks);

  m_notebookPath = m_tempDir->filePath(QStringLiteral("nb"));
  m_notebookId = m_notebooks->createNotebook(
      m_notebookPath, QStringLiteral(R"({"name": "Import NB"})"), NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestImportFolderDialog2::cleanup() {
  if (m_notebooks && !m_notebookId.isEmpty()) {
    m_notebooks->closeNotebook(m_notebookId);
  }
  delete m_notebooks;
  m_notebooks = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  delete m_tempDir;
  m_tempDir = nullptr;
}

NodeIdentifier TestImportFolderDialog2::rootOf(const QString &p_notebookId) const {
  NodeIdentifier id;
  id.notebookId = p_notebookId;
  id.relativePath = QStringLiteral(".");
  return id;
}

bool TestImportFolderDialog2::okEnabled(ImportFolderDialog2 &p_dialog) {
  auto *box = p_dialog.findChild<QDialogButtonBox *>();
  if (!box) {
    return false;
  }
  auto *ok = box->button(QDialogButtonBox::Ok);
  return ok && ok->isEnabled();
}

QString TestImportFolderDialog2::bannerText(ImportFolderDialog2 &p_dialog) {
  auto *edit = p_dialog.findChild<QPlainTextEdit *>();
  return edit ? edit->toPlainText() : QString();
}

void TestImportFolderDialog2::testStartsInExternalModeWithOkDisabled() {
  ImportFolderDialog2 dialog(m_services, rootOf(m_notebookId));

  auto *externalRadio = dialog.findChild<QRadioButton *>(QStringLiteral("externalModeRadio"));
  auto *bundleRadio = dialog.findChild<QRadioButton *>(QStringLiteral("bundleModeRadio"));
  auto *stack = dialog.findChild<QStackedWidget *>(QStringLiteral("modeStack"));
  QVERIFY(externalRadio);
  QVERIFY(bundleRadio);
  QVERIFY(stack);

  QVERIFY(externalRadio->isChecked());
  QVERIFY(!bundleRadio->isChecked());
  QCOMPARE(dialog.currentMode(), ImportFolderDialog2::ExternalMode);
  QCOMPARE(stack->currentIndex(), static_cast<int>(ImportFolderDialog2::ExternalMode));

  // Nothing has been chosen yet, so Ok must stay disabled in either mode.
  QVERIFY(!okEnabled(dialog));
}

void TestImportFolderDialog2::testModeSwitchSwapsTheStackAndClearsTheBanner() {
  ImportFolderDialog2 dialog(m_services, rootOf(m_notebookId));

  auto *externalRadio = dialog.findChild<QRadioButton *>(QStringLiteral("externalModeRadio"));
  auto *bundleRadio = dialog.findChild<QRadioButton *>(QStringLiteral("bundleModeRadio"));
  auto *stack = dialog.findChild<QStackedWidget *>(QStringLiteral("modeStack"));
  QVERIFY(externalRadio);
  QVERIFY(bundleRadio && bundleRadio->isEnabled());
  QVERIFY(stack);

  bundleRadio->setChecked(true);
  QCOMPARE(dialog.currentMode(), ImportFolderDialog2::BundleMode);
  QCOMPARE(stack->currentIndex(), static_cast<int>(ImportFolderDialog2::BundleMode));

  // Produce an error banner in bundle mode: a plain directory is not a bundle.
  const QString bogus = m_tempDir->filePath(QStringLiteral("not-a-bundle"));
  QVERIFY(QDir().mkpath(bogus + QStringLiteral("/Alpha")));
  auto *bundleInput = dialog.findChild<QWidget *>(QStringLiteral("bundlePathInput"));
  QVERIFY(bundleInput);
  auto *edit = bundleInput->findChild<QLineEdit *>();
  QVERIFY(edit);
  edit->setText(bogus);
  QVERIFY(!bannerText(dialog).isEmpty());

  // Switching back must clear whatever the other page put in the banner, so a
  // stale message is never attributed to the newly selected mode.
  externalRadio->setChecked(true);
  QCOMPARE(dialog.currentMode(), ImportFolderDialog2::ExternalMode);
  QCOMPARE(stack->currentIndex(), static_cast<int>(ImportFolderDialog2::ExternalMode));
  QVERIFY(bannerText(dialog).isEmpty());
}

void TestImportFolderDialog2::testHiddenPageSignalsDoNotEnableOk() {
  ImportFolderDialog2 dialog(m_services, rootOf(m_notebookId));

  auto *bundleRadio = dialog.findChild<QRadioButton *>(QStringLiteral("bundleModeRadio"));
  QVERIFY(bundleRadio && bundleRadio->isEnabled());

  // Stay in EXTERNAL mode, but poke the bundle page's line edit. Its handler
  // early-returns on the mode mismatch, so Ok must not move.
  auto *bundleInput = dialog.findChild<QWidget *>(QStringLiteral("bundlePathInput"));
  QVERIFY(bundleInput);
  auto *edit = bundleInput->findChild<QLineEdit *>();
  QVERIFY(edit);

  QCOMPARE(dialog.currentMode(), ImportFolderDialog2::ExternalMode);
  edit->setText(m_tempDir->filePath(QStringLiteral("whatever")));
  QVERIFY(!okEnabled(dialog));
}

void TestImportFolderDialog2::testBundleRadioDisabledForARawNotebook() {
  // Bundle import is structurally bundled-only: a raw notebook has no
  // vx_notebook/contents/ tree, so the mode is offered but explained away.
  const QString rawPath = m_tempDir->filePath(QStringLiteral("raw-nb"));
  const QString rawId = m_notebooks->createNotebook(
      rawPath, QStringLiteral(R"({"name": "Raw NB"})"), NotebookType::Raw);
  QVERIFY(!rawId.isEmpty());

  ImportFolderDialog2 dialog(m_services, rootOf(rawId));
  auto *bundleRadio = dialog.findChild<QRadioButton *>(QStringLiteral("bundleModeRadio"));
  QVERIFY(bundleRadio);
  QVERIFY(!bundleRadio->isEnabled());
  QVERIFY(!bundleRadio->toolTip().isEmpty());
  QCOMPARE(dialog.currentMode(), ImportFolderDialog2::ExternalMode);

  m_notebooks->closeNotebook(rawId);
}

void TestImportFolderDialog2::testBundleRadioDisabledForAReadOnlyNotebook() {
  QCOMPARE(vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), 1),
           VXCORE_OK);

  ImportFolderDialog2 dialog(m_services, rootOf(m_notebookId));
  auto *bundleRadio = dialog.findChild<QRadioButton *>(QStringLiteral("bundleModeRadio"));
  QVERIFY(bundleRadio);
  QVERIFY(!bundleRadio->isEnabled());
  QVERIFY(!bundleRadio->toolTip().isEmpty());
}

void TestImportFolderDialog2::testInvalidBundlePathIsRejectedWithAMessage() {
  ImportFolderDialog2 dialog(m_services, rootOf(m_notebookId));

  auto *bundleRadio = dialog.findChild<QRadioButton *>(QStringLiteral("bundleModeRadio"));
  QVERIFY(bundleRadio && bundleRadio->isEnabled());
  bundleRadio->setChecked(true);

  auto *bundleInput = dialog.findChild<QWidget *>(QStringLiteral("bundlePathInput"));
  QVERIFY(bundleInput);
  auto *edit = bundleInput->findChild<QLineEdit *>();
  QVERIFY(edit);

  const QString plain = m_tempDir->filePath(QStringLiteral("plain"));
  QVERIFY(QDir().mkpath(plain + QStringLiteral("/Alpha")));
  edit->setText(plain);

  QVERIFY(!okEnabled(dialog));
  QVERIFY(!bannerText(dialog).isEmpty());

  auto *preview = dialog.findChild<QLabel *>(QStringLiteral("bundlePreviewLabel"));
  QVERIFY(preview);
  QVERIFY(preview->text().isEmpty());
}

} // namespace tests

QTEST_MAIN(tests::TestImportFolderDialog2)
#include "test_import_folder_dialog2.moc"
