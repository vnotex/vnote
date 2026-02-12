#ifndef CONFIGMGR2_H
#define CONFIGMGR2_H

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "iconfigmgr.h"
#include "noncopyable.h"

class QTimer;

namespace vnotex {
class ConfigService;

// Thin DI-ready configuration manager wrapper over ConfigService.
// Receives ConfigService via constructor for dependency injection.
// Provides path accessors and config persistence with debouncing.
// Does NOT manage MainConfig/SessionConfig - those can be added later when needed.
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

  // Constructor receives ConfigService via DI (non-owning pointer).
  // ConfigService must remain valid for the lifetime of this manager.
  explicit ConfigMgr2(ConfigService *p_configService, QObject *p_parent = nullptr);

  ~ConfigMgr2();

  // Initialize configuration system
  void init();

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

  // Non-owning pointer to ConfigService (managed by caller)
  ConfigService *m_configService = nullptr;

  // Debounced write timers (500ms)
  QTimer *m_mainConfigWriteTimer = nullptr;
  QTimer *m_sessionConfigWriteTimer = nullptr;

  // Pending writes
  QJsonObject m_pendingMainConfig;
  QJsonObject m_pendingSessionConfig;

  // Cached paths
  QString m_appDataPath;
  QString m_userConfigPath;
};

} // namespace vnotex

#endif // CONFIGMGR2_H
