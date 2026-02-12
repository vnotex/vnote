#ifndef MAINWINDOW2_H
#define MAINWINDOW2_H

#include <QMainWindow>

#include <core/noncopyable.h>

namespace vnotex {

class ServiceLocator;

// MainWindow2 is a minimal QMainWindow shell for the new clean architecture.
// Receives ServiceLocator via constructor for dependency injection.
// Framework only - NO toolbar, dock widgets, menu bar, or status bar.
// Widgets will be added incrementally during migration.
class MainWindow2 : public QMainWindow, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives ServiceLocator reference via DI.
  // @p_serviceLocator: Reference to ServiceLocator (non-owning). Must outlive MainWindow2.
  // @p_parent: Optional QWidget parent.
  explicit MainWindow2(ServiceLocator &p_serviceLocator, QWidget *p_parent = nullptr);

  ~MainWindow2();

  // Access to ServiceLocator for child widgets that need services.
  ServiceLocator &getServiceLocator() const;

private:
  // Setup basic window properties (title, size, central widget).
  void setupUI();

  // Non-owning reference to ServiceLocator.
  ServiceLocator &m_serviceLocator;
};

} // namespace vnotex

#endif // MAINWINDOW2_H
