#include "updatelease.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGlobalStatic>
#include <QThread>

#include <utility>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

using namespace vnotex;

constexpr int UpdateLease::c_defaultTimeoutMs;
constexpr int UpdateLease::c_pollIntervalMs;

namespace {

const QString c_leaseFileName = QStringLiteral(".vnote-update.lease");

// Diagnostics only; not part of the locking protocol.
Q_GLOBAL_STATIC(QString, g_lastAcquireDetail)

void setDetail(const QString &p_detail) { *g_lastAcquireDetail() = p_detail; }

#ifdef Q_OS_WIN

// Errors that mean "somebody else currently holds it": keep polling.
//
// ERROR_DELETE_PENDING is a REAL transient state, not a failure: it is what the
// kernel reports while a FILE_FLAG_DELETE_ON_CLOSE handle is unwinding, i.e.
// exactly while the previous holder is releasing.
bool isHeldByOther(DWORD p_error) {
  switch (p_error) {
  case ERROR_SHARING_VIOLATION:
  case ERROR_ACCESS_DENIED:
  case ERROR_LOCK_VIOLATION:
  case ERROR_DELETE_PENDING:
    return true;
  default:
    return false;
  }
}

QString formatWinError(DWORD p_error) {
  return QStringLiteral("win32 error %1").arg(static_cast<quint32>(p_error));
}

#endif // Q_OS_WIN

} // namespace

UpdateLease::~UpdateLease() { release(); }

UpdateLease::UpdateLease(UpdateLease &&p_other) noexcept { moveFrom(std::move(p_other)); }

UpdateLease &UpdateLease::operator=(UpdateLease &&p_other) noexcept {
  if (this != &p_other) {
    release();
    moveFrom(std::move(p_other));
  }
  return *this;
}

void UpdateLease::moveFrom(UpdateLease &&p_other) noexcept {
  m_path = p_other.m_path;
  p_other.m_path.clear();
#ifdef Q_OS_WIN
  m_handle = p_other.m_handle;
  p_other.m_handle = nullptr;
#else
  m_fd = p_other.m_fd;
  p_other.m_fd = -1;
#endif
}

QString UpdateLease::leasePath(const QString &p_installDir) {
  if (p_installDir.isEmpty()) {
    return QString();
  }
  return QDir::cleanPath(p_installDir) + QLatin1Char('/') + c_leaseFileName;
}

QString UpdateLease::lastAcquireDetail() {
  return g_lastAcquireDetail.exists() ? *g_lastAcquireDetail() : QString();
}

bool UpdateLease::isHeld() const {
#ifdef Q_OS_WIN
  return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
#else
  return m_fd >= 0;
#endif
}

void UpdateLease::release() {
  if (!isHeld()) {
    m_path.clear();
    return;
  }

#ifdef Q_OS_WIN
  // FILE_FLAG_DELETE_ON_CLOSE removes the sentinel as the handle closes.
  ::CloseHandle(static_cast<HANDLE>(m_handle));
  m_handle = nullptr;
#else
  // Unlink first so a waiter that already has the path open does not resurrect
  // a stale sentinel, then close (which drops the flock).
  if (!m_path.isEmpty()) {
    ::unlink(QFile::encodeName(m_path).constData());
  }
  ::close(m_fd);
  m_fd = -1;
#endif

  m_path.clear();
}

UpdateLease UpdateLease::acquire(const QString &p_installDir, int p_timeoutMs,
                                 AcquireError *p_error) {
  UpdateLease lease;
  auto fail = [&](AcquireError p_code, const QString &p_detail) -> UpdateLease {
    setDetail(p_detail);
    if (p_error) {
      *p_error = p_code;
    }
    return UpdateLease();
  };

  const QString path = leasePath(p_installDir);
  if (path.isEmpty()) {
    return fail(AcquireError::Fatal, QStringLiteral("empty install directory"));
  }

  // The install directory must already exist; we never create it here.
  if (!QFileInfo(QDir::cleanPath(p_installDir)).isDir()) {
    return fail(AcquireError::Fatal,
                QStringLiteral("install directory '%1' does not exist").arg(p_installDir));
  }

  QElapsedTimer timer;
  timer.start();

  for (;;) {
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(path);
    HANDLE handle = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()),
                                  GENERIC_READ | GENERIC_WRITE,
                                  0, // dwShareMode = 0 -> exclusive, machine-wide
                                  nullptr, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);

    if (handle != INVALID_HANDLE_VALUE) {
      lease.m_handle = handle;
      lease.m_path = path;

      // Diagnostics only. Failure to write the PID must not fail acquisition:
      // the lease is the handle, not the contents.
      const QByteArray pid = QByteArray::number(static_cast<qint64>(::GetCurrentProcessId()));
      DWORD written = 0;
      ::WriteFile(handle, pid.constData(), static_cast<DWORD>(pid.size()), &written, nullptr);
      ::FlushFileBuffers(handle);

      setDetail(QString());
      if (p_error) {
        *p_error = AcquireError::None;
      }
      return lease;
    }

    const DWORD err = ::GetLastError();
    if (!isHeldByOther(err)) {
      // Fail closed on anything we did not explicitly classify as contention.
      return fail(AcquireError::Fatal, formatWinError(err));
    }
#else
    const QByteArray nativePath = QFile::encodeName(path);
    const int fd = ::open(nativePath.constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
      return fail(AcquireError::Fatal,
                  QStringLiteral("open('%1') failed: errno %2").arg(path).arg(errno));
    }

    if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
      lease.m_fd = fd;
      lease.m_path = path;

      // Diagnostics only.
      const QByteArray pid = QByteArray::number(static_cast<qint64>(::getpid()));
      if (::ftruncate(fd, 0) == 0) {
        const ssize_t ignored = ::write(fd, pid.constData(), static_cast<size_t>(pid.size()));
        Q_UNUSED(ignored);
      }

      setDetail(QString());
      if (p_error) {
        *p_error = AcquireError::None;
      }
      return lease;
    }

    const int lockErrno = errno;
    ::close(fd);
    if (lockErrno != EWOULDBLOCK && lockErrno != EAGAIN && lockErrno != EINTR) {
      return fail(AcquireError::Fatal,
                  QStringLiteral("flock('%1') failed: errno %2").arg(path).arg(lockErrno));
    }
#endif

    if (p_timeoutMs <= 0 || timer.elapsed() >= p_timeoutMs) {
      return fail(AcquireError::Timeout,
                  QStringLiteral("lease '%1' still held after %2 ms").arg(path).arg(p_timeoutMs));
    }

    const int remaining = p_timeoutMs - static_cast<int>(timer.elapsed());
    QThread::msleep(static_cast<unsigned long>(qBound(1, qMin(c_pollIntervalMs, remaining), c_pollIntervalMs)));
  }
}

qint64 UpdateLease::readOwnerPidForDiagnostics(const QString &p_installDir) {
  const QString path = leasePath(p_installDir);
  if (path.isEmpty()) {
    return 0;
  }

  // Open shared-read; this must never disturb the holder.
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return 0;
  }
  const QByteArray data = file.read(64).trimmed();
  file.close();

  bool ok = false;
  const qint64 pid = data.toLongLong(&ok);
  return ok ? pid : 0;
}
