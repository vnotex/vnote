#ifndef MAINWINDOW2_H
#define MAINWINDOW2_H

#include <QMainWindow>

#include <core/noncopyable.h>
#include <widgets/dockwidgethelper.h>

class QCloseEvent;
class QDockWidget;
class QWebEngineView;

namespace vnotex {

class ServiceLocator;
class NotebookExplorer2;

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
  ServiceLocator &getServiceLocator();

  // Access NotebookExplorer2.
  NotebookExplorer2 *getNotebookExplorer() const;

  QWidget *getDockWidget(DockWidgetHelper::DockType p_dockType) const;

  void kickOffPostInit(const QStringList &p_pathsToOpen);

signals:
    void layoutChanged();

protected:
  void closeEvent(QCloseEvent *p_event) override;

private:
  // Setup basic window properties (title, size, central widget).
  void setupUI();

  // Setup NotebookExplorer2 as dock widget.
  void setupNotebookExplorer();

  // Setup dock widgets.
  void setupDocks();

  void loadStateAndGeometry();
  void saveStateAndGeometry();

  // Non-owning reference to ServiceLocator.
  ServiceLocator &m_serviceLocator;

  DockWidgetHelper m_dockWidgetHelper{this};

  // NotebookExplorer2 dock widget.
  NotebookExplorer2 *m_notebookExplorer = nullptr;

#if defined(Q_OS_WIN)
  QWebEngineView *m_dummyWebView = nullptr;
#endif
};

} // namespace vnotex

#endif // MAINWINDOW2_H
