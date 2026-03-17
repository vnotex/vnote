#ifndef CONFIGMGR2_H
#define CONFIGMGR2_H

#include <QJsonObject>
#include <QObject>
#include <QScopedPointer>
#include <QString>
#include <QVersionNumber>

#include "iconfigmgr.h"
#include "noncopyable.h"

class QTimer;

namespace vnotex {
class ConfigCoreService;
class MainConfig;
class SessionConfig;
class CoreConfig;
class EditorConfig;
class WidgetConfig;

// Thin DI-ready configuration manager wrapper over ConfigCoreService.
// Receives ConfigCoreService via constructor for dependency injection.
// Provides path accessors and config persistence with debouncing.
// Owns MainConfig and SessionConfig instances.
class ConfigMgr2 : public QObject, public IConfigMgr, private Noncopyable {
  Q_OBJECT

public:
  enum ConfigDataType {
    Main,
    Themes,
    Tasks,
    WebStyles,
    SyntaxHighlighting,
    Dicts,
    Templates,
    Snippets,
    Web
  };

  // Constructor receives ConfigCoreService via DI (non-owning pointer).
  // ConfigCoreService must remain valid for the lifetime of this manager.
  explicit ConfigMgr2(ConfigCoreService *p_configService, QObject *p_parent = nullptr);

  ~ConfigMgr2();

  // Initialize configuration system.
  // Loads MainConfig and SessionConfig from disk.
  // Must be called after construction and before using configs.
  void init();

  void initAfterQtAppStarted();

  // Get main configuration.
  MainConfig &getConfig();
  const MainConfig &getConfig() const;

  // Get session configuration.
  SessionConfig &getSessionConfig();
  const SessionConfig &getSessionConfig() const;

  // Convenience accessors for child configs within MainConfig.
  CoreConfig &getCoreConfig();
  const CoreConfig &getCoreConfig() const;
  EditorConfig &getEditorConfig();
  WidgetConfig &getWidgetConfig();

  // Get path to specific config data folder
  QString getConfigDataFolder(ConfigDataType p_type) const;

  // Get path to application data folder
  QString getAppDataPath() const;

  // Get path to user config folder
  QString getUserConfigPath() const;

  // Get log file path
  QString getLogFile() const;

  // Get markdown user style file path (web/css/user.css)
  QString getMarkdownUserStyleFile() const;

  // If @p_filePath is absolute, just return it.
  // Otherwise, resolve relative to config folder.
  QString getFileFromConfigFolder(const QString &p_filePath) const;

  // Parse exp like "[main|session].core.shortcuts.FullScreen" and return the config value.
  QJsonValue parseAndReadConfig(const QString &p_exp) const;

  // Get application version string.
  static QString getApplicationVersion();

  // Get application file path.
  static QString getApplicationFilePath();

  // Get document or home path.
  static QString getDocumentOrHomePath();

  // Organization name.
  static const QString c_orgName;

  // Application name.
  static const QString c_appName;

signals:
  void editorConfigChanged();

public:
  // IConfigMgr interface implementation
  // Used by IConfig to trigger config persistence
  void updateMainConfig(const QJsonObject &p_jobj) override;
  void updateSessionConfig(const QJsonObject &p_jobj) override;

private slots:
  // Debounced write handlers (called after timer expires)
  void doWriteMainConfig();
  void doWriteSessionConfig();

private:
  // Schedule debounced writes
  void scheduleMainConfigWrite();
  void scheduleSessionConfigWrite();

  // Perform version upgrade (copy themes, tasks, etc.).
  void upgradeMainConfigOnVersionChange();

  // Initialize app prefix search paths.
  void initAppPrefixPath();

  // Non-owning pointer to ConfigCoreService (managed by caller)
  ConfigCoreService *m_configService = nullptr;

  // Owned config instances
  QScopedPointer<MainConfig> m_mainConfig;
  QScopedPointer<SessionConfig> m_sessionConfig;

  // Whether version changed since last run
  bool m_versionChanged = false;

  // Debounced write timers (500ms)
  QTimer *m_mainConfigWriteTimer = nullptr;
  QTimer *m_sessionConfigWriteTimer = nullptr;

  // Pending writes
  QJsonObject m_pendingMainConfig;
  QJsonObject m_pendingSessionConfig;

  // Cached paths
  QString m_appDataPath;
  QString m_localDataPath;

  // Application version
  static const QVersionNumber c_version;
};

} // namespace vnotex

#endif // CONFIGMGR2_H
