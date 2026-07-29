#ifndef CORE_CONSTANTS_H
#define CORE_CONSTANTS_H

namespace vnotex {
// Exit code asking main() to spawn a replacement process after teardown.
constexpr int kExitToRestart = 1000;

// Exit code asking main() to APPLY a staged incremental update (see
// UpdateInstaller) after every service, ConfigMgr2 and Application have been
// destroyed, and only then spawn the replacement process.
constexpr int kExitToApplyUpdate = 1001;
} // namespace vnotex

#endif // CORE_CONSTANTS_H
