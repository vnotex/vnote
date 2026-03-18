#ifndef CONFIGSERVICE_H
#define CONFIGSERVICE_H

#include <core/services/configcoreservice.h>

namespace vnotex {

class HookManager;

// Hook-aware wrapper around ConfigCoreService.
// Manages application lifecycle persistence via MainWindowBeforeClose/ShutdownCancelled hooks.
// Follows the same pattern as BufferService wrapping BufferCoreService.
class ConfigService : private ConfigCoreService {
  Q_OBJECT

public:
  explicit ConfigService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                         QObject *p_parent = nullptr);

  // Expose the underlying ConfigCoreService for consumers that need it
  // (e.g., ConfigMgr2, ServiceLocator dual registration).
  ConfigCoreService *coreService();
  const ConfigCoreService *coreService() const;

  // Expose QObject base for signal connections from outside.
  QObject *asQObject();

  // ============ Pass-through API (expose ConfigCoreService public methods) ============

  using ConfigCoreService::getExecutionFilePath;
  using ConfigCoreService::getExecutionFolderPath;
  using ConfigCoreService::getDataPath;
  using ConfigCoreService::getConfigPath;
  using ConfigCoreService::getSessionConfigPath;
  using ConfigCoreService::getConfig;
  using ConfigCoreService::getSessionConfig;
  using ConfigCoreService::getConfigByName;
  using ConfigCoreService::getConfigByNameWithDefaults;
  using ConfigCoreService::updateConfigByName;
  using ConfigCoreService::isRecoverLastSessionEnabled;
  using ConfigCoreService::setRecoverLastSessionEnabled;

private:
  HookManager *m_hookMgr = nullptr;
};

} // namespace vnotex

#endif // CONFIGSERVICE_H
