#ifndef TOOLBARHELPER2_H
#define TOOLBARHELPER2_H

#include <QIcon>
#include <QObject>

#include <core/global.h>
#include <core/sessionconfig.h>

class QMenu;
class QToolBar;

namespace vnotex {

class MainWindow2;
class ServiceLocator;
class UnitedEntry;
class UnitedEntryMgr;

// Tool bar helper for MainWindow2.
// Non-static design with dependency injection for testability.
class ToolBarHelper2 : public QObject {
  Q_OBJECT

public:
  // Constructor receives dependencies via DI.
  // @p_services: ServiceLocator reference for theme/config access.
  // @p_mainWindow: Parent MainWindow2 for signal connections.
  explicit ToolBarHelper2(ServiceLocator &p_services, MainWindow2 *p_mainWindow);

  // Setup all tool bars of main window.
  void setupToolBars();

  // Setup tool bars of main window into one unified tool bar.
  void setupToolBars(QToolBar *p_toolBar);

  // Generate icon from theme.
  QIcon generateIcon(const QString &p_iconName);

  // Generate dangerous/warning icon from theme.
  QIcon generateDangerousIcon(const QString &p_iconName);

  // Access the UnitedEntryMgr instance (for signal wiring).
  UnitedEntryMgr *unitedEntryMgr() const;

  // Add spacer to toolbar.
  static void addSpacer(QToolBar *p_toolBar);

private:
  QToolBar *setupFileToolBar(QToolBar *p_toolBar);

  QToolBar *setupSettingsToolBar(QToolBar *p_toolBar);

  void setupUnitedEntry(QToolBar *p_toolBar);

  void updateQuickAccessMenu(QMenu *p_menu);

  void setupExpandButton(QToolBar *p_toolBar);

  void setupSettingsButton(QToolBar *p_toolBar);

  void activateQuickAccess(const SessionConfig::QuickAccessItem &p_item);

  void activateQuickAccessFilePath(const QString &p_file, ViewWindowMode p_mode);

  void activateQuickAccessFromVxUrl(const QString &p_vxUrl);

  QToolBar *createToolBar(const QString &p_title, const QString &p_name);

  ServiceLocator &m_services;

  MainWindow2 *m_mainWindow;

  UnitedEntry *m_unitedEntry = nullptr;

  UnitedEntryMgr *m_unitedEntryMgr = nullptr;
};

} // namespace vnotex

#endif // TOOLBARHELPER2_H
