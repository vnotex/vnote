// Link-time stubs for test_updatecontroller.
//
// UpdateController opens exactly one widget-layer class of its own,
// UpdateDialog. Linking the real updatedialog.cpp would drag the themed widget
// stack into a test that never opens a dialog, so it is stubbed here -- the
// same approach as tests/unitedentry/stubs_findunitedentry.cpp and
// tests/core/stubs_textviewwindowcontroller.cpp.
//
// The controller no longer knows about MainWindow2 (it takes a plain QWidget*
// dialog parent), so there is nothing else to stub.

#include <widgets/dialogs/updatedialog.h>

using namespace vnotex;

UpdateDialog::UpdateDialog(ServiceLocator &p_services, const UpdateInfo &p_info, QWidget *p_parent)
    : QDialog(p_parent) {
  Q_UNUSED(p_services)
  Q_UNUSED(p_info)
}

void UpdateDialog::setFailed(const QString &p_message) { Q_UNUSED(p_message) }

#ifdef Q_OS_WIN
void UpdateDialog::setUpdateInProgress() {}
#endif

void UpdateDialog::setupUi(ServiceLocator &p_services) { Q_UNUSED(p_services) }
