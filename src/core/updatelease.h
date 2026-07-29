#ifndef UPDATELEASE_H
#define UPDATELEASE_H

#include <QString>
#include <QtGlobal>

namespace vnotex {

// Machine-wide, cross-session mutual exclusion for the incremental updater.
//
// The lease is an EXCLUSIVELY OPENED SENTINEL FILE at
// `<installDir>/.vnote-update.lease`. On Windows it is a `CreateFileW` handle
// with `dwShareMode = 0` and `FILE_FLAG_DELETE_ON_CLOSE`; on other platforms it
// is an `flock(LOCK_EX | LOCK_NB)` on an open descriptor. In both cases the OS
// releases it when the owning process dies, including on a kill or a crash.
//
// Why not the obvious alternatives (see the plan's "Update lease" section):
//
//   - `SingleInstanceGuard` is scope-local, owns a `QLocalServer` that must not
//     outlive `QApplication`, and was historically FAIL-OPEN.
//   - `QLockFile` defaults to a 30 s stale timeout (a long apply can have its
//     lock stolen), has a PID-reuse hole, and living inside `.vnote-update/`
//     conflicts with deleting that directory at commit. The sentinel is a
//     SIBLING of `.vnote-update/` precisely so committing can remove the whole
//     directory without a lock-vs-directory ordering race.
//   - A `Local\` named mutex is scoped to one Terminal Services session, so
//     fast-user-switching or RDP against a shared portable install would run two
//     unserialized appliers. `Global\` requires `SeCreateGlobalPrivilege`, which
//     standard users do not have.
//
// The owner PID is written into the file for DIAGNOSTICS ONLY. It is never used
// for liveness: PIDs are reused, the kernel handle is the truth.
//
// Move-only, not a QObject, and usable before `QCoreApplication` exists (it is
// acquired as the first executable statement of `main()`).
class UpdateLease {
public:
  static constexpr int c_defaultTimeoutMs = 60 * 1000;
  static constexpr int c_pollIntervalMs = 100;

  enum class AcquireError {
    // Acquired.
    None,
    // Another process held the lease for the whole timeout window.
    Timeout,
    // Anything else: bad path, permission problem, unexpected OS error. The
    // caller MUST fail closed -- never fall through to normal initialization.
    Fatal,
  };

  UpdateLease() = default;
  ~UpdateLease();

  UpdateLease(UpdateLease &&p_other) noexcept;
  UpdateLease &operator=(UpdateLease &&p_other) noexcept;

  UpdateLease(const UpdateLease &) = delete;
  UpdateLease &operator=(const UpdateLease &) = delete;

  // Bounded poll-loop acquisition. "Held by someone else" is retried every
  // c_pollIntervalMs until p_timeoutMs elapses; any other OS error fails
  // immediately (fail-closed).
  //
  // p_timeoutMs == 0 attempts exactly once.
  static UpdateLease acquire(const QString &p_installDir,
                             int p_timeoutMs = c_defaultTimeoutMs,
                             AcquireError *p_error = nullptr);

  static QString leasePath(const QString &p_installDir);

  // Human-readable last-error detail from the most recent failed acquire() on
  // this thread. Diagnostics only.
  static QString lastAcquireDetail();

  bool isHeld() const;

  explicit operator bool() const { return isHeld(); }

  // Explicit release. REQUIRED on the restart path: `exit(0)` does not unwind
  // C++ objects, so the destructor would never run.
  void release();

  const QString &path() const { return m_path; }

  // Owner PID recorded in an EXISTING lease file. Diagnostics only; returns 0
  // when the file is absent or unreadable. Never use this for liveness.
  //
  // NOTE (Windows): while the lease is held the sentinel is open with
  // `dwShareMode = 0`, so this necessarily returns 0 -- nobody, including the
  // diagnostic reader, can open it. It is informative only in the rare case
  // where the file outlived its holder (power loss before the kernel unwound
  // `FILE_FLAG_DELETE_ON_CLOSE`), and on the POSIX `flock` fallback where the
  // sentinel is readable while locked.
  static qint64 readOwnerPidForDiagnostics(const QString &p_installDir);

private:
  void moveFrom(UpdateLease &&p_other) noexcept;

  QString m_path;

#ifdef Q_OS_WIN
  // HANDLE, kept as void* so <windows.h> stays out of this header.
  void *m_handle = nullptr;
#else
  int m_fd = -1;
#endif
};

} // namespace vnotex

#endif // UPDATELEASE_H
