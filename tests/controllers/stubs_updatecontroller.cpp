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

namespace tests {
int g_restartForUpdateCalls = 0;
}

using namespace vnotex;

// UpdateController only uses MainWindow2 as an opaque pointer: it calls the
// non-virtual restartForUpdate() and passes the pointer as a dialog parent. It
// never connects to a MainWindow2 signal, so MainWindow2's moc/vtable is NOT
// needed here -- and mainwindow2.h is deliberately NOT listed in this target's
// AUTOMOC sources (see CMakeLists.txt) so its vtable is not emitted. Emitting it
// would force every out-of-line virtual AND every member-object destructor
// (DockWidgetHelper, NavigationMode, ...) to be linked, dragging in the whole
// widget tree this stub exists to avoid. Only restartForUpdate() must exist.
void MainWindow2::restartForUpdate() { ++tests::g_restartForUpdateCalls; }

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
