#ifndef CONFIGMGR_H
#define CONFIGMGR_H

#include <QJsonObject>
#include <QObject>
#include <QScopedPointer>

#include "noncopyable.h"

class QTimer;

typedef struct VxCoreContext *VxCoreContextHandle;

namespace vnotex {
class MainConfig;
class SessionConfig;
class CoreConfig;
class EditorConfig;
class WidgetConfig;

class ConfigMgr : public QObject, private Noncopyable {
  Q_OBJECT
public:
  enum class Source { App, Session };

  ~ConfigMgr();

  static ConfigMgr &getInst();

  MainConfig &getConfig();

  SessionConfig &getSessionConfig();

  CoreConfig &getCoreConfig();

  EditorConfig &getEditorConfig();

  WidgetConfig &getWidgetConfig();

  QString getAppFolder() const;

  QString getUserFolder() const;

  QString getLogFile() const;

  QString getAppThemeFolder() const;

  QString getUserThemeFolder() const;

  QString getAppTaskFolder() const;

  QString getUserTaskFolder() const;

  QString getAppWebStylesFolder() const;

  QString getUserWebStylesFolder() const;

  QString getAppDocsFolder() const;

  QString getUserDocsFolder() const;

  QString getAppSyntaxHighlightingFolder() const;

  QString getUserSyntaxHighlightingFolder() const;

  QString getAppDictsFolder() const;
  QString getUserDictsFolder() const;

  QString getUserTemplateFolder() const;

  QString getUserSnippetFolder() const;

  // web/css/user.css.
  QString getUserMarkdownUserStyleFile() const;

  // If @p_filePath is absolute, just return it.
  // Otherwise, first try to find it in user folder, then in app folder.
  QString getUserOrAppFile(const QString &p_filePath) const;

  QString getConfigFilePath(Source p_src) const;

  // Parse exp like "[main|session].core.shortcuts.FullScreen" and return the config value.
  QJsonValue parseAndReadConfig(const QString &p_exp) const;

  // Called at boostrap without QApplication instance.
  static QString locateSessionConfigFilePathAtBootstrap();

  static QString getApplicationFilePath();

  static QString getApplicationDirPath();

  static QString getDocumentOrHomePath();

  static QString getApplicationVersion();

  static void initAppPrefixPath();

  static const QString c_orgName;

  static const QString c_appName;

public:
  // Used by IConfig.
  void writeUserSettings(const QJsonObject &p_jobj);

  void writeSessionSettings(const QJsonObject &p_jobj);

signals:
  void editorConfigChanged();

private:
  ConfigMgr(QObject *p_parent = nullptr);

  // Locate the folder path where the config file exists.
  void locateConfigFolder();

  // Check if app config exists and is updated.
  // Update it if in need.
  // Return true if there is update.
  bool checkAppConfig();

  void checkUserConfig();

  static QString getDefaultConfigFilePath();

  // Schedule debounced writes
  void scheduleMainConfigWrite();
  void scheduleSessionConfigWrite();

private slots:
  // Actual write handlers (called after timer expires)
  void doWriteMainConfig();
  void doWriteSessionConfig();

private:
  QScopedPointer<MainConfig> m_config;

  // Session config.
  QScopedPointer<SessionConfig> m_sessionConfig;

  // vxcore context for JSON I/O
  VxCoreContextHandle m_vxcoreContext;

  // Debounced write timers (500ms)
  QTimer *m_mainConfigWriteTimer;
  QTimer *m_sessionConfigWriteTimer;

  // Config folder paths
  QString m_appConfigFolderPath;
  QString m_userConfigFolderPath;

  // Pending writes
  QJsonObject m_pendingMainConfig;
  QJsonObject m_pendingSessionConfig;
};
} // namespace vnotex

#endif // CONFIGMGR_H
