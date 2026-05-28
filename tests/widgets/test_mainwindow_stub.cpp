// Stub implementations for vnotex::MainWindow.
//
// The widgets dialog stack (notebooksyncinfodialog2 -> scrolldialog -> dialog
// -> WidgetUtils -> safelyOpenUrl) transitively references MainWindow::tr(),
// which expands via Q_OBJECT to use MainWindow::staticMetaObject. AUTOMOC
// processes mainwindow.h (added to SOURCES in tests/widgets/CMakeLists.txt)
// to provide that staticMetaObject. moc_mainwindow.cpp in turn references
// MainWindow's constructor/destructor and private slot bodies — which live
// in mainwindow.cpp, a heavyweight file we cannot link from this test (its
// transitive dependencies pull in nearly the entire vnote.exe object graph).
//
// This stub file supplies the minimum bodies the linker requires so the test
// binary links cleanly. It is NOT a functional MainWindow — none of its
// slots will ever be called during the test runs because the dialog under
// test never instantiates MainWindow and the only call site of MainWindow::tr
// (WidgetUtils::safelyOpenUrl) is not exercised by the dialog flow.

#include <widgets/mainwindow.h>

namespace vnotex {

MainWindow::MainWindow(QWidget *p_parent) : QMainWindow(p_parent) {}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *p_event) { Q_UNUSED(p_event); }

void MainWindow::changeEvent(QEvent *p_event) { Q_UNUSED(p_event); }

void MainWindow::closeOnQuit() {}

void MainWindow::exportNotes() {}

void MainWindow::showTips(const QString &p_message, int p_timeoutMilliseconds) {
  Q_UNUSED(p_message);
  Q_UNUSED(p_timeoutMilliseconds);
}

} // namespace vnotex
