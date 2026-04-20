#ifndef MAINWINDOW2_H
#define MAINWINDOW2_H

#include <QMainWindow>

#include <core/noncopyable.h>
#include <widgets/dockwidgethelper.h>

class QCloseEvent;
class QDockWidget;
class QToolBar;
class QWebEngineView;
class QProgressDialog;
class QSystemTrayIcon;

namespace QWK {
class WidgetWindowAgent;
}

namespace vnotex {

class ServiceLocator;
class NotebookExplorer2;
class OutlineViewer;
class TagExplorer2;
class SnippetPanel2;
class SearchPanel2;
class LocationList2;
class ViewArea2;
class ExportDialog2;

class ToolBarHelper2;
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

  // Access ViewArea2.
  ViewArea2 *getViewArea() const;

  QWidget *getDockWidget(DockWidgetHelper::DockType p_dockType) const;

  void kickOffPostInit(const QStringList &p_pathsToOpen);

  void setupNavigationMode();

  // Content area expansion.
  bool isContentAreaExpanded() const;
  void setContentAreaExpanded(bool p_expanded);

  // Window state.
  void setStayOnTop(bool p_enabled);

  bool isFrameless() const;

  // Access dock widgets.
  const QVector<QDockWidget *> &getDocks() const;

  // Reset window state and geometry.
  void resetStateAndGeometry();

  void restart();

  void showMainWindow();

  void quitApp();

signals:
  void windowStateChanged(Qt::WindowStates p_state);

  void layoutChanged();

  void minimizedToSystemTray();

  // File operations.
  void newNoteRequested();
  void newQuickNoteRequested();
  void newFolderRequested();
  void importFileRequested();
  void importFolderRequested();
  void exportRequested();

protected:
  void closeEvent(QCloseEvent *p_event) override;

  void changeEvent(QEvent *p_event) Q_DECL_OVERRIDE;

private:
  // Setup basic window properties (title, size, central widget).
  void setupUI();

  // Setup NotebookExplorer2 as dock widget.
  void setupNotebookExplorer();

  // Setup OutlineViewer as dock widget.
  void setupOutlineViewer();

  // Setup TagExplorer2 as dock widget.
  void setupTagExplorer();

  // Setup SnippetPanel2 as dock widget.
  void setupSnippetExplorer();

  // Setup SearchPanel2 as dock widget.
  void setupSearchPanel();

  // Setup LocationList2 as dock widget.
  void setupLocationList();

  // Setup ViewArea2 as central widget.
  void setupViewArea();

  // Setup dock widgets.
  void setupDocks();

  // Setup tool bar.
  void setupToolBar();

  // Setup qwindowkit window agent for frameless mode.
  void setupWindowAgent();

  void exportNotes();

  void setupSystemTray();

  // Restore only window geometry and dock state (safe to call before event loop).
  void restoreWindowGeometry();

  // Restore explorer state and view area layout (must be called after show()).
  void loadStateAndGeometry();

  void saveStateAndGeometry();

  // Theme switch orchestration slot.
  void onThemeChanged();

  // Non-owning reference to ServiceLocator.
  ServiceLocator &m_serviceLocator;

  DockWidgetHelper m_dockWidgetHelper{this, m_serviceLocator};

  // NotebookExplorer2 dock widget.
  NotebookExplorer2 *m_notebookExplorer = nullptr;

  // OutlineViewer dock widget.
  OutlineViewer *m_outlineViewer = nullptr;

  // TagExplorer2 dock widget.
  TagExplorer2 *m_tagExplorer = nullptr;

  // SnippetPanel2 dock widget.
  SnippetPanel2 *m_snippetPanel = nullptr;

  // SearchPanel2 dock widget.
  SearchPanel2 *m_searchPanel = nullptr;

  // LocationList2 dock widget.
  LocationList2 *m_locationList = nullptr;

  // ViewArea2 central widget.
  ViewArea2 *m_viewArea = nullptr;

  // Toolbar helper.
  ToolBarHelper2 *m_toolBarHelper = nullptr;

  ExportDialog2 *m_exportDialog = nullptr;

  QSystemTrayIcon *m_trayIcon = nullptr;

  // Theme switch progress dialog.
  QProgressDialog *m_progressDialog = nullptr;

  // Content area expanded state.
  bool m_contentAreaExpanded = false;

  // Dock visibility saved before content area expansion, for restoring later.
  QStringList m_visibleDocksBeforeExpand;

  bool m_layoutReset = false;

  // -1: do not request to quit;
  // 0 and above: exit code.
  int m_requestQuit = -1;

  Qt::WindowStates m_windowOldState = Qt::WindowMinimized;

  QWK::WidgetWindowAgent *m_windowAgent = nullptr;

  bool m_frameless = false;

#if defined(Q_OS_WIN)
  QWebEngineView *m_dummyWebView = nullptr;
#endif
};

} // namespace vnotex

#endif // MAINWINDOW2_H
