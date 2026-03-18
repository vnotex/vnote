#include "configservice.h"

#include <QDebug>

#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

ConfigService::ConfigService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                             QObject *p_parent)
    : ConfigCoreService(p_context, p_parent), m_hookMgr(p_hookMgr) {
  if (m_hookMgr) {
    // Snapshot session state on close — priority 100 (runs last, after ViewArea2's
    // tab-order sync at priority 10).
    m_hookMgr->addAction(
        HookNames::MainWindowBeforeClose,
        [this](HookContext &p_ctx, const QVariantMap &p_args) {
          Q_UNUSED(p_ctx)
          Q_UNUSED(p_args)
          qInfo() << "ConfigService: MainWindowBeforeClose -> prepareShutdown";
          prepareShutdown();
        },
        100);

    // Resume normal operation if shutdown is cancelled.
    m_hookMgr->addAction(
        HookNames::MainWindowShutdownCancelled,
        [this](HookContext &p_ctx, const QVariantMap &p_args) {
          Q_UNUSED(p_ctx)
          Q_UNUSED(p_args)
          qInfo() << "ConfigService: MainWindowShutdownCancelled -> cancelShutdown";
          cancelShutdown();
        },
        100);
  }
}

ConfigCoreService *ConfigService::coreService() {
  return static_cast<ConfigCoreService *>(this);
}

const ConfigCoreService *ConfigService::coreService() const {
  return static_cast<const ConfigCoreService *>(this);
}

QObject *ConfigService::asQObject() { return this; }
