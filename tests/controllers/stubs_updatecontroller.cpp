// Link-time stubs for test_updatecontroller.
//
// UpdateController calls into two widget-layer classes: MainWindow2 (restart)
// and UpdateDialog (the modal check surface). Linking the real
// mainwindow2.cpp would pull in essentially the whole widget/controller/model
// tree for two symbols, so stub them here instead -- the same approach as
// tests/unitedentry/stubs_findunitedentry.cpp and
// tests/core/stubs_textviewwindowcontroller.cpp.
//
// The test never opens a dialog and never restarts, so these bodies only have
// to exist. `restartCalls` is exposed so a future test can assert the restart
// path without resurrecting MainWindow2.

#include <widgets/dialogs/updatedialog.h>
#include <widgets/mainwindow2.h>

#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>

namespace tests {
int g_restartForUpdateCalls = 0;
}

using namespace vnotex;

void MainWindow2::restartForUpdate() { ++tests::g_restartForUpdateCalls; }

// GCC/Clang emit MainWindow2's vtable (via AUTOMOC on mainwindow2.h) in this TU
// and require every out-of-line virtual it lists to be defined, or the link
// fails with "undefined reference to vnotex::MainWindow2::~MainWindow2()" etc.
// (MSVC tolerates the missing slots, which is why only the Unix CI caught this.)
// The test never constructs a MainWindow2, so these bodies only have to exist.
MainWindow2::~MainWindow2() {}

void MainWindow2::closeEvent(QCloseEvent *p_event) { Q_UNUSED(p_event) }

void MainWindow2::changeEvent(QEvent *p_event) { Q_UNUSED(p_event) }

void MainWindow2::dragEnterEvent(QDragEnterEvent *p_event) { Q_UNUSED(p_event) }

void MainWindow2::dropEvent(QDropEvent *p_event) { Q_UNUSED(p_event) }

UpdateDialog::UpdateDialog(const UpdateInfo &p_info, QWidget *p_parent) : QDialog(p_parent) {
  Q_UNUSED(p_info)
}

void UpdateDialog::setDownloading() {}

void UpdateDialog::setProgress(const QString &p_stage, qint64 p_done, qint64 p_total) {
  Q_UNUSED(p_stage)
  Q_UNUSED(p_done)
  Q_UNUSED(p_total)
}

void UpdateDialog::setReadyToApply(const QString &p_version) { Q_UNUSED(p_version) }

void UpdateDialog::setFailed(const QString &p_message) { Q_UNUSED(p_message) }

void UpdateDialog::setupUi() {}
