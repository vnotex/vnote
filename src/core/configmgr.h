#ifndef CONFIGMGR_H
#define CONFIGMGR_H

#include <QJsonObject>
#include <QObject>
#include <QScopedPointer>

#include "global.h"
#include "iconfigmgr.h"
#include "noncopyable.h"

class QTimer;

namespace vnotex {
class MainConfig;
class SessionConfig;
class CoreConfig;
class EditorConfig;
class WidgetConfig;

// DEPRECATED: Use ConfigMgr2 with ServiceLocator pattern instead.
// This class is kept for reference during migration but its functionality is disabled.
class VNOTEX_DEPRECATED("Use ConfigMgr2 with ServiceLocator pattern instead") ConfigMgr
    : public QObject,
      public IConfigMgr,
      private Noncopyable {
  Q_OBJECT
public:
  enum ConfigDataType { Main, Themes, Tasks, WebStyles, SyntaxHighlighting, Dicts,
    Templates, Snippets, Web };

  ~ConfigMgr();

  static ConfigMgr &getInst();

  void initAfterQtAppStarted();

  MainConfig &getConfig();

  SessionConfig &getSessionConfig();

  CoreConfig &getCoreConfig();

  EditorConfig &getEditorConfig();

  WidgetConfig &getWidgetConfig();

  QString getConfigDataFolder(ConfigDataType p_type) const;

  QString getLogFile() const;

  // web/css/user.css.
  QString getMarkdownUserStyleFile() const;

  // If @p_filePath is absolute, just return it.
  QString getFileFromConfigFolder(const QString &p_filePath) const;

  // Parse exp like "[main|session].core.shortcuts.FullScreen" and return the config value.
  QJsonValue parseAndReadConfig(const QString &p_exp) const;

  static QString getApplicationFilePath();

  static QString getDocumentOrHomePath();

  static QString getApplicationVersion();

  static const QString c_orgName;

  static const QString c_appName;

signals:
  void editorConfigChanged();

public:
  // IConfigMgr interface implementation
  // Used by IConfig.
  void updateMainConfig(const QJsonObject &p_jobj) override;
  void updateSessionConfig(const QJsonObject &p_jobj) override;

private:
  ConfigMgr(QObject *p_parent = nullptr);

  void upgradeMainConfigOnVersionChange();

  // Schedule debounced writes
  void scheduleMainConfigWrite();
  void scheduleSessionConfigWrite();

  QJsonObject loadDefaultMainConfig() const;

  static void initAppPrefixPath();

private slots:
  // Actual write handlers (called after timer expires)
  void doWriteMainConfig();
  void doWriteSessionConfig();

private:
  QScopedPointer<MainConfig> m_mainConfig;

  QScopedPointer<SessionConfig> m_sessionConfig;

  bool m_version_changed = false;

  // Debounced write timers (500ms)
  QTimer *m_mainConfigWriteTimer = nullptr;
  QTimer *m_sessionConfigWriteTimer = nullptr;

  // Pending writes
  QJsonObject m_pendingMainConfig;
  QJsonObject m_pendingSessionConfig;

  QString m_configFolderPath;
};
} // namespace vnotex

#endif // CONFIGMGR_H
