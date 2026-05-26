#ifndef VXCORELOGBRIDGE_H
#define VXCORELOGBRIDGE_H

namespace vnotex {

// Installs the vxcore log handler so vxcore logs flow into Qt's logging
// pipeline. Call ONCE from main(), BEFORE the first vxcore API call, so even
// the earliest vxcore log lines are captured.
void installVxCoreLogBridge();

// Uninstalls the bridge and restores vxcore's default stderr/file sinks.
void uninstallVxCoreLogBridge();

} // namespace vnotex

#endif // VXCORELOGBRIDGE_H
