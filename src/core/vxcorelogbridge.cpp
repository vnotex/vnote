#include "vxcorelogbridge.h"

#include <QMessageLogger>
#include <QtGlobal>
#include <vxcore/vxcore_log.h>

namespace {

void vxcoreLogThunk(VxCoreLogLevel level, const char *file, int line, const char *message,
                    void *userdata) {
  Q_UNUSED(userdata);

  switch (level) {
  case VXCORE_LOG_LEVEL_TRACE:
  case VXCORE_LOG_LEVEL_DEBUG:
    QMessageLogger(file, line, nullptr).debug("%s", message);
    break;

  case VXCORE_LOG_LEVEL_INFO:
    QMessageLogger(file, line, nullptr).info("%s", message);
    break;

  case VXCORE_LOG_LEVEL_WARN:
    QMessageLogger(file, line, nullptr).warning("%s", message);
    break;

  case VXCORE_LOG_LEVEL_ERROR:
  case VXCORE_LOG_LEVEL_FATAL:
    QMessageLogger(file, line, nullptr).critical("%s", message);
    break;

  case VXCORE_LOG_LEVEL_OFF:
    // Defensive: no-op for OFF level
    break;

  default:
    break;
  }
}

} // namespace

namespace vnotex {

void installVxCoreLogBridge() { vxcore_log_set_handler(&vxcoreLogThunk, nullptr); }

void uninstallVxCoreLogBridge() { vxcore_log_set_handler(nullptr, nullptr); }

} // namespace vnotex
