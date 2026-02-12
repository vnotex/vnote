#ifndef ICONFIGMGR_H
#define ICONFIGMGR_H

#include <QJsonObject>

namespace vnotex {

// Interface for configuration managers to support IConfig callback pattern.
// Both ConfigMgr and ConfigMgr2 implement this interface so that
// config classes (MainConfig, SessionConfig, etc.) can call back
// without depending on a specific manager implementation.
class IConfigMgr {
public:
  virtual ~IConfigMgr() = default;

  // Called by IConfig instances when configuration changes need to be persisted
  virtual void updateMainConfig(const QJsonObject &p_jobj) = 0;
  virtual void updateSessionConfig(const QJsonObject &p_jobj) = 0;
};

} // namespace vnotex

#endif // ICONFIGMGR_H
