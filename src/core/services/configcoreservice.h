#ifndef CONFIGCORESERVICE_H
#define CONFIGCORESERVICE_H

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "core/error.h"
#include "core/noncopyable.h"

#include <vxcore/vxcore_types.h>

namespace vnotex {

// Qt wrapper for data locations matching VxCoreDataLocation.
enum class DataLocation { App = VXCORE_DATA_APP, Local = VXCORE_DATA_LOCAL };

// Service wrapper for vxcore configuration API.
// Provides Qt-friendly interface with Error return codes for write operations.
// Receives VxCoreContextHandle via constructor for dependency injection.
class ConfigCoreService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives VxCoreContextHandle via DI.
  // The context handle must remain valid for the lifetime of this service.
  explicit ConfigCoreService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);

  // Get path to executable file.
  QString getExecutionFilePath() const;

  // Get path to folder containing executable.
  QString getExecutionFolderPath() const;

  // Get data path for specified location.
  QString getDataPath(DataLocation p_location) const;

  // Get path to main config file.
  QString getConfigPath() const;

  // Get path to session config file.
  QString getSessionConfigPath() const;

  // Get main configuration as QJsonObject.
  QJsonObject getConfig() const;

  // Get session configuration as QJsonObject.
  QJsonObject getSessionConfig() const;

  // Get configuration by name for specified location.
  QJsonObject getConfigByName(DataLocation p_location, const QString &p_baseName) const;

  // Get configuration by name with default values fallback.
  QJsonObject getConfigByNameWithDefaults(DataLocation p_location, const QString &p_baseName,
                                          const QJsonObject &p_defaults) const;

  // Update configuration by name. Returns Error for failure cases.
  Error updateConfigByName(DataLocation p_location, const QString &p_baseName,
                           const QJsonObject &p_json);

  // Get recoverLastSession setting from vxcore config.
  bool isRecoverLastSessionEnabled() const;

  // Set recoverLastSession setting in vxcore config and persist.
  bool setRecoverLastSessionEnabled(bool p_enabled);

  // Get autoSyncDebounceSeconds setting from vxcore config.
  // Returns clamped value in range [0, 86400].
  int getAutoSyncDebounceSeconds() const;

  // Set autoSyncDebounceSeconds setting in vxcore config and persist.
  // Clamps p_seconds to range [0, 86400] before writing.
  // Returns true on success, false on failure.
  bool setAutoSyncDebounceSeconds(int p_seconds);

  // Set the app-wide locale used by vxcore for locale-aware, UTF-8 output
  // (currently the built-in %MMM%/%MMMM%/%ddd%/%dddd% snippets). Accepts
  // Qt/POSIX-style names such as "en_US", "zh_CN", "ja"; unknown or empty
  // resolves to English.
  // RUNTIME-ONLY: unlike the rest of this class, this setting is NOT part of the
  // persisted config JSON. It must be pushed again on every app start.
  bool setAppLocale(const QString &p_locale);

  // Canonical locale name currently in effect ("en" | "zh_CN" | "ja").
  QString appLocale() const;

  // Snapshot current session state to disk. Sets shutdown flag to prevent
  // destructor double-save. Idempotent.
  bool prepareShutdown();

  // Reset the shutdown flag so normal mutations resume. Safe to call in any state.
  bool cancelShutdown();

private:
  // Convert C string to QString and free the C string using vxcore_string_free.
  static QString cstrToQString(char *p_str);

  // Parse JSON string to QJsonObject from C string (takes ownership, frees p_str).
  static QJsonObject parseJsonObjectFromCStr(char *p_str);

  // Check if context is valid.
  bool checkContext() const;

  VxCoreContextHandle m_context = nullptr;
};

} // namespace vnotex

#endif // CONFIGCORESERVICE_H
