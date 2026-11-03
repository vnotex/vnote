#include "apicredentialstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <aclapi.h>
#include <io.h>
#include <sddl.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace vnotex;

namespace {
constexpr qint64 c_maxRegistryBytes = 1024 * 1024;

bool fail(QString *p_error, const char *p_code) {
  if (p_error) {
    *p_error = QLatin1String(p_code);
  }
  return false;
}

#ifdef Q_OS_WIN
class NativeHandle {
public:
  explicit NativeHandle(HANDLE p_handle = INVALID_HANDLE_VALUE) : m_handle(p_handle) {}
  ~NativeHandle() {
    if (m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr) {
      CloseHandle(m_handle);
    }
  }
  NativeHandle(const NativeHandle &) = delete;
  NativeHandle &operator=(const NativeHandle &) = delete;
  NativeHandle(NativeHandle &&p_other) noexcept
      : m_handle(std::exchange(p_other.m_handle, INVALID_HANDLE_VALUE)) {}
  HANDLE get() const { return m_handle; }
  bool valid() const { return m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr; }

private:
  HANDLE m_handle;
};

class LocalMemory {
public:
  explicit LocalMemory(void *p_value = nullptr) : value(p_value) {}
  ~LocalMemory() {
    if (value) {
      LocalFree(value);
    }
  }
  LocalMemory(const LocalMemory &) = delete;
  LocalMemory &operator=(const LocalMemory &) = delete;
  void *value;
};

bool currentUser(QByteArray *p_sid, QString *p_error) {
  HANDLE raw = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
    return fail(p_error, "credential_store_identity");
  }
  NativeHandle token(raw);
  DWORD bytes = 0;
  GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
  if (!bytes || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return fail(p_error, "credential_store_identity");
  }
  QByteArray info(static_cast<qsizetype>(bytes), '\0');
  if (!GetTokenInformation(token.get(), TokenUser, info.data(), bytes, &bytes)) {
    return fail(p_error, "credential_store_identity");
  }
  const auto *user = reinterpret_cast<const TOKEN_USER *>(info.constData());
  const DWORD length = GetLengthSid(user->User.Sid);
  p_sid->resize(static_cast<qsizetype>(length));
  if (!CopySid(length, p_sid->data(), user->User.Sid)) {
    return fail(p_error, "credential_store_identity");
  }
  return true;
}

bool regularHandle(HANDLE p_handle, bool p_directory, QString *p_error) {
  FILE_ATTRIBUTE_TAG_INFO info{};
  if (!GetFileInformationByHandleEx(p_handle, FileAttributeTagInfo, &info, sizeof(info)) ||
      (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      bool(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != p_directory) {
    return fail(p_error, "credential_store_path");
  }
  return true;
}

bool privateHandle(HANDLE p_handle, const QByteArray &p_sid, bool p_directory, QString *p_error) {
  if (!regularHandle(p_handle, p_directory, p_error)) {
    return false;
  }
  PSID owner = nullptr;
  PACL acl = nullptr;
  PSECURITY_DESCRIPTOR raw = nullptr;
  const DWORD error = GetSecurityInfo(p_handle, SE_FILE_OBJECT,
                                      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                      &owner, nullptr, &acl, nullptr, &raw);
  LocalMemory security(raw);
  if (error != ERROR_SUCCESS || !owner || !acl ||
      !EqualSid(owner, const_cast<char *>(p_sid.constData())) || !IsValidAcl(acl)) {
    return fail(p_error, "credential_store_permissions");
  }
  bool ownerAccess = false;
  for (DWORD i = 0; i < acl->AceCount; ++i) {
    void *rawAce = nullptr;
    if (!GetAce(acl, i, &rawAce)) {
      return fail(p_error, "credential_store_permissions");
    }
    const auto *header = static_cast<const ACE_HEADER *>(rawAce);
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
      // This dedicated store has one simple owner ACL. Do not infer safety
      // from unfamiliar object/conditional ACEs or an inherited broad grant.
      return fail(p_error, "credential_store_permissions");
    }
    const auto *ace = static_cast<const ACCESS_ALLOWED_ACE *>(rawAce);
    if (!EqualSid(const_cast<DWORD *>(&ace->SidStart), const_cast<char *>(p_sid.constData()))) {
      return fail(p_error, "credential_store_permissions");
    }
    if (!(header->AceFlags & INHERIT_ONLY_ACE) &&
        (ace->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS) {
      ownerAccess = true;
    }
  }
  if (!ownerAccess) {
    return fail(p_error, "credential_store_permissions");
  }
  if (p_directory) {
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(raw, &control, &revision) || !(control & SE_DACL_PROTECTED)) {
      return fail(p_error, "credential_store_permissions");
    }
  }
  return true;
}

bool makePrivateDirectory(const QString &p_path, const QByteArray &p_sid, QString *p_error) {
  LPWSTR sidText = nullptr;
  if (!ConvertSidToStringSidW(const_cast<char *>(p_sid.constData()), &sidText)) {
    return fail(p_error, "credential_store_identity");
  }
  LocalMemory sidOwner(sidText);
  const QString sid = QString::fromWCharArray(sidText);
  const auto sddl = QStringLiteral("O:%1D:P(A;OICI;FA;;;%1)").arg(sid);
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr)) {
    return fail(p_error, "credential_store_permissions");
  }
  LocalMemory descriptorOwner(descriptor);
  SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
  const auto native = QDir::toNativeSeparators(p_path);
  if (!CreateDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()), &attributes) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    return fail(p_error, "credential_store_create");
  }
  return true;
}

struct DirectoryGuard {
  QByteArray sid;
  std::vector<NativeHandle> handles;
  bool exists = false;
};

bool openDirectory(const QString &p_path, bool p_create, DirectoryGuard *p_guard,
                   QString *p_error) {
  if (!currentUser(&p_guard->sid, p_error)) {
    return false;
  }
  const QString path = QDir::cleanPath(QFileInfo(p_path).absoluteFilePath());
  // Network shares/reparse roots cannot provide this same-machine profile
  // boundary. Remote clients connect through the gateway, not a shared token file.
  if (path.startsWith(QStringLiteral("//")) || path.size() < 4 || path.at(1) != QLatin1Char(':')) {
    return fail(p_error, "credential_store_path");
  }
  const auto parts = path.mid(3).split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QString current = path.left(3);
  for (qsizetype i = 0; i <= parts.size(); ++i) {
    if (i > 0) {
      current = QDir(current).filePath(parts.at(i - 1));
    }
    const bool last = i == parts.size();
    const auto native = QDir::toNativeSeparators(current);
    HANDLE raw =
        CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES | READ_CONTROL,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE && last && GetLastError() == ERROR_FILE_NOT_FOUND) {
      if (!p_create) {
        return true;
      }
      if (!makePrivateDirectory(current, p_guard->sid, p_error)) {
        return false;
      }
      raw = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),
                        FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    }
    NativeHandle handle(raw);
    if (!handle.valid() || !regularHandle(handle.get(), true, p_error)) {
      return fail(p_error, "credential_store_path");
    }
    if (last && !privateHandle(handle.get(), p_guard->sid, true, p_error)) {
      return false;
    }
    p_guard->handles.push_back(std::move(handle));
  }
  p_guard->exists = true;
  return true;
}

bool readRegistry(const QString &p_path, const DirectoryGuard &p_guard, QByteArray *p_bytes,
                  bool *p_exists, QString *p_error) {
  const auto native = QDir::toNativeSeparators(p_path);
  NativeHandle file(CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ,
                                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!file.valid()) {
    if (GetLastError() == ERROR_FILE_NOT_FOUND) {
      *p_exists = false;
      return true;
    }
    return fail(p_error, "credential_store_read");
  }
  *p_exists = true;
  if (!privateHandle(file.get(), p_guard.sid, false, p_error)) {
    return false;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
      size.QuadPart > c_maxRegistryBytes) {
    return fail(p_error, "credential_store_size");
  }
  if (!p_bytes) {
    return true;
  }
  p_bytes->resize(static_cast<qsizetype>(size.QuadPart));
  DWORD readBytes = 0;
  if (!ReadFile(file.get(), p_bytes->data(), static_cast<DWORD>(p_bytes->size()), &readBytes,
                nullptr) ||
      readBytes != static_cast<DWORD>(p_bytes->size())) {
    return fail(p_error, "credential_store_read");
  }
  return true;
}

bool privateTemporaryFile(QSaveFile &p_file, const DirectoryGuard &p_guard, QString *p_error) {
  const auto raw = _get_osfhandle(p_file.handle());
  if (raw == -1) {
    return fail(p_error, "credential_store_permissions");
  }
  const auto file = reinterpret_cast<HANDLE>(raw);
  if (!regularHandle(file, false, p_error)) {
    return false;
  }
  // Elevated tokens can default new files to the Administrators owner. Set the
  // owner only on our new, still-empty temporary file; existing stores remain
  // subject to strict ownership checks. Reopen by handle, never by pathname.
  NativeHandle security(ReOpenFile(file, READ_CONTROL | WRITE_OWNER,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0));
  if (!security.valid() ||
      SetSecurityInfo(security.get(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                      const_cast<char *>(p_guard.sid.constData()), nullptr, nullptr,
                      nullptr) != ERROR_SUCCESS) {
    return fail(p_error, "credential_store_permissions");
  }
  return privateHandle(security.get(), p_guard.sid, false, p_error);
}
#else
class NativeHandle {
public:
  explicit NativeHandle(int p_fd = -1) : m_fd(p_fd) {}
  ~NativeHandle() {
    if (m_fd >= 0) {
      ::close(m_fd);
    }
  }
  NativeHandle(const NativeHandle &) = delete;
  NativeHandle &operator=(const NativeHandle &) = delete;
  NativeHandle(NativeHandle &&p_other) noexcept : m_fd(std::exchange(p_other.m_fd, -1)) {}
  int get() const { return m_fd; }

private:
  int m_fd;
};

struct DirectoryGuard {
  std::vector<NativeHandle> handles;
  bool exists = false;
};

bool privateFile(int p_fd, bool p_directory, QString *p_error) {
  struct stat info{};
  if (fstat(p_fd, &info) != 0 || info.st_uid != geteuid() || (info.st_mode & 0077) ||
      (p_directory ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode))) {
    return fail(p_error, "credential_store_permissions");
  }
  return true;
}

bool openDirectory(const QString &p_path, bool p_create, DirectoryGuard *p_guard,
                   QString *p_error) {
  const auto path = QDir::cleanPath(QFileInfo(p_path).absoluteFilePath());
  const auto parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  const int root = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (root < 0 || parts.isEmpty()) {
    if (root >= 0) {
      ::close(root);
    }
    return fail(p_error, "credential_store_path");
  }
  p_guard->handles.emplace_back(root);
  for (qsizetype i = 0; i < parts.size(); ++i) {
    const bool last = i + 1 == parts.size();
    const auto name = QFile::encodeName(parts.at(i));
    const int parent = p_guard->handles.back().get();
    int fd = openat(parent, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && last && errno == ENOENT) {
      if (!p_create) {
        return true;
      }
      if (mkdirat(parent, name.constData(), 0700) != 0 && errno != EEXIST) {
        return fail(p_error, "credential_store_create");
      }
      fd = openat(parent, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    }
    if (fd < 0) {
      return fail(p_error, "credential_store_path");
    }
    p_guard->handles.emplace_back(fd);
    if (last && !privateFile(fd, true, p_error)) {
      return false;
    }
  }
  p_guard->exists = true;
  return true;
}

bool readRegistry(const QString &, const DirectoryGuard &p_guard, QByteArray *p_bytes,
                  bool *p_exists, QString *p_error) {
  const int fd =
      openat(p_guard.handles.back().get(), "tokens.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) {
    if (errno == ENOENT) {
      *p_exists = false;
      return true;
    }
    return fail(p_error, "credential_store_read");
  }
  NativeHandle file(fd);
  *p_exists = true;
  struct stat info{};
  if (!privateFile(fd, false, p_error)) {
    return false;
  }
  if (fstat(fd, &info) != 0 || info.st_size < 0 || info.st_size > c_maxRegistryBytes) {
    return fail(p_error, "credential_store_size");
  }
  if (!p_bytes) {
    return true;
  }
  p_bytes->resize(static_cast<qsizetype>(info.st_size));
  qsizetype offset = 0;
  while (offset < p_bytes->size()) {
    const auto count =
        ::read(fd, p_bytes->data() + offset, static_cast<size_t>(p_bytes->size() - offset));
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      return fail(p_error, "credential_store_read");
    }
    offset += count;
  }
  return true;
}

bool privateTemporaryFile(QSaveFile &p_file, const DirectoryGuard &, QString *p_error) {
  if (fchmod(p_file.handle(), 0600) != 0) {
    return fail(p_error, "credential_store_permissions");
  }
  return privateFile(p_file.handle(), false, p_error);
}
#endif
} // namespace

ApiCredentialStore::ApiCredentialStore(QString p_directory)
    : m_directory(QDir::cleanPath(QFileInfo(p_directory).absoluteFilePath())) {}

bool ApiCredentialStore::load(QJsonArray *p_records, QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  if (!p_records) {
    return fail(p_error, "credential_store_argument");
  }
  *p_records = {};
  DirectoryGuard directory;
  if (!openDirectory(m_directory, false, &directory, p_error)) {
    return false;
  }
  if (!directory.exists) {
    return true;
  }
  QByteArray bytes;
  bool exists = false;
  if (!readRegistry(QDir(m_directory).filePath(QStringLiteral("tokens.json")), directory, &bytes,
                    &exists, p_error)) {
    return false;
  }
  if (!exists) {
    return true;
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(bytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
    return fail(p_error, "credential_store_corrupt");
  }
  *p_records = document.array();
  return true;
}

bool ApiCredentialStore::save(const QJsonArray &p_records, QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  const auto bytes = QJsonDocument(p_records).toJson(QJsonDocument::Compact);
  if (bytes.size() > c_maxRegistryBytes) {
    return fail(p_error, "credential_store_size");
  }
  DirectoryGuard directory;
  if (!openDirectory(m_directory, true, &directory, p_error) || !directory.exists) {
    return false;
  }
  const auto path = QDir(m_directory).filePath(QStringLiteral("tokens.json"));
  // Verify an existing target before QSaveFile can replace it. Never overwrite a
  // symlink or a registry whose ownership/security differs from this profile.
  bool exists = false;
  if (!readRegistry(path, directory, nullptr, &exists, p_error)) {
    return false;
  }
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    return fail(p_error, "credential_store_write");
  }
  if (!privateTemporaryFile(file, directory, p_error)) {
    file.cancelWriting();
    return false;
  }
  if (file.write(bytes) != bytes.size() || !file.commit()) {
    file.cancelWriting();
    return fail(p_error, "credential_store_write");
  }
  if (!readRegistry(path, directory, nullptr, &exists, p_error) || !exists) {
    return fail(p_error, "credential_store_verify");
  }
  return true;
}
