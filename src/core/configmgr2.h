#ifndef CONFIGMGR2_H
#define CONFIGMGR2_H

#include <QJsonObject>
#include <QObject>
#include <QScopedPointer>
#include <QString>
#include <QStringList>
#include <QVector>
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

  // One bundled extra-data folder whose install did not fully succeed during
  // the last ensureExtraData() run. Surfaced to the user by NotificationRouter
  // at MainWindowAfterStart.
  struct ExtraDataFailure {
    // Bundle folder name, e.g. "web".
    QString m_folderName;

    // Message of the FIRST error hit while installing that folder.
    QString m_errorMessage;

    // Paths involved in the failure: normally the source paths that failed to
    // copy, or the destination stamp path when the stamp itself could not be
    // removed or written. Empty when the failure was not per-path (e.g. the
    // bundle resource could not be registered at all).
    QStringList m_failedPaths;
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

  // Returns true if the persisted config version differs from the current
  // application version (computed once during init()). Stays true for the
  // whole session even after the new version is persisted.
  bool isVersionChanged() const;

  // Folders whose bundled extra data could not be fully installed during the
  // last ensureExtraData() call (i.e. during initAfterQtAppStarted()). Empty
  // when everything installed cleanly. A folder that failed AFTER its stamp was
  // invalidated is left unstamped on disk and is retried on the next launch; a
  // folder that failed because the bundle itself was unavailable keeps whatever
  // was already installed (there was nothing to install from).
  const QVector<ExtraDataFailure> &extraDataCopyFailures() const;

  // Override the root directory the bundled extra data is copied FROM. For
  // testing only, so scenarios run against an on-disk fixture instead of
  // mounting the production vnote_extra.rcc (which is not built in the
  // unit-test targets). When set, the rcc is NOT registered.
  void setExtraDataSourceRootOverrideForTesting(const QString &p_root);

  // Get application file path.
  static QString getApplicationFilePath();

  // Get document or home path.
  static QString getDocumentOrHomePath();

  // Organization name.
  static const QString c_orgName;

  // Application name.
  static const QString c_appName;

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

  // Perform version upgrade of the config itself (version-gated forced
  // overrides + version stamping). The bundled extra-data dump is NOT part of
  // this; it is owned by ensureExtraData(), which runs on every launch.
  void upgradeMainConfigOnVersionChange();

  // Install every bundled extra-data folder (themes, tasks,
  // syntax-highlighting, web, dicts, templates) into the app data folder, one
  // per-folder version stamp at a time. Called on EVERY launch: a folder that is already
  // stamped with the current version is a cheap no-op, and a folder whose copy
  // previously failed (leaving it unstamped) is retried here.
  // @p_force: re-copy even when the stamp already matches.
  // Records per-folder failures in m_extraDataFailures (cleared on entry).
  void ensureExtraData(bool p_force);

  // Initialize app prefix search paths.
  void initAppPrefixPath();

  // Non-owning pointer to ConfigCoreService (managed by caller)
  ConfigCoreService *m_configService = nullptr;

  // Owned config instances
  QScopedPointer<MainConfig> m_mainConfig;
  QScopedPointer<SessionConfig> m_sessionConfig;

  // Whether version changed since last run
  bool m_versionChanged = false;

  // Folders whose extra-data install failed during the last ensureExtraData().
  QVector<ExtraDataFailure> m_extraDataFailures;

  // Test-only override for the bundled extra-data source root.
  QString m_extraDataSourceRootOverride;

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
