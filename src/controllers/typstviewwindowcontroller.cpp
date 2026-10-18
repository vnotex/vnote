#include "typstviewwindowcontroller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QStringView>
#include <QVarLengthArray>

#include <core/fileopensettings.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <vxcore/notebook_json_keys.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace vnotex;

namespace {
// As in legacy image migration, Qt canonical paths alone do not resolve NTFS junctions.
// Unlike that comparison helper, unresolved paths must fail closed here.
QString resolvedFileSystemPath(const QString &p_path) {
#ifdef Q_OS_WIN
  const auto native = QDir::toNativeSeparators(p_path);
  const HANDLE handle = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return {};
  }
  QVarLengthArray<wchar_t, MAX_PATH> buffer(MAX_PATH);
  DWORD written = ::GetFinalPathNameByHandleW(handle, buffer.data(), buffer.size(),
                                              FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (written >= static_cast<DWORD>(buffer.size())) {
    buffer.resize(static_cast<int>(written) + 1);
    written = ::GetFinalPathNameByHandleW(handle, buffer.data(), buffer.size(),
                                          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  }
  ::CloseHandle(handle);
  if (written == 0 || written >= static_cast<DWORD>(buffer.size())) {
    return {};
  }
  auto result = QString::fromWCharArray(buffer.data(), static_cast<int>(written));
  if (result.startsWith(QStringLiteral("\\\\?\\UNC\\"))) {
    result = QStringLiteral("\\\\") + result.mid(8);
  } else if (result.startsWith(QStringLiteral("\\\\?\\"))) {
    result.remove(0, 4);
  }
  return QDir::fromNativeSeparators(result);
#else
  return QFileInfo(p_path).canonicalFilePath();
#endif
}

// Match the native helper's project paths, without normalizing forbidden input away.
bool isProjectPath(const QString &p_path) {
  if (p_path.isEmpty() || p_path.startsWith(QLatin1Char('/')) ||
      p_path.startsWith(QLatin1Char('@')) || p_path.contains(QChar::Null) ||
      p_path.contains(QLatin1Char('\\')) || p_path.contains(QLatin1Char(':'))) {
    return false;
  }
  const QStringView path(p_path);
  qsizetype start = 0;
  while (start <= path.size()) {
    const auto slash = path.indexOf(QLatin1Char('/'), start);
    const auto end = slash < 0 ? path.size() : slash;
    const auto segment = path.mid(start, end - start);
    if (segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String("..")) {
      return false;
    }
    if (slash < 0) {
      return true;
    }
    start = slash + 1;
  }
  return false;
}

bool externalSchemeAllowed(const QUrl &p_url) {
  const auto scheme = p_url.scheme();
  return scheme == QStringLiteral("http") || scheme == QStringLiteral("https") ||
         scheme == QStringLiteral("ftp") || scheme == QStringLiteral("mailto");
}

bool containsNull(const QUrl &p_url) {
  // Check every component, including encoded NULs in queries and fragments.
  return p_url.toString(QUrl::FullyEncoded).contains(QLatin1String("%00"), Qt::CaseInsensitive);
}

bool isLocalLinkPath(const QString &p_path) {
  if (p_path.isEmpty() || p_path.contains(QChar::Null) || p_path.startsWith(QLatin1String("//")) ||
      p_path.contains(QLatin1Char('\\'))) {
    return false;
  }
  // A drive-relative path must never resolve against the process's current drive directory.
  return !(p_path.size() >= 2 && p_path.at(1) == QLatin1Char(':') && !QDir::isAbsolutePath(p_path));
}
} // namespace

TypstViewWindowController::TypstViewWindowController(ServiceLocator &p_services, QObject *p_parent)
    : TextViewWindowController(p_services, p_parent), m_services(p_services) {}

bool TypstViewWindowController::setBuffer(const Buffer2 &p_buffer, QString *p_error) {
  ++m_generation;
  m_pendingExternalLink = QUrl();
  m_pendingExternalGeneration = 0;
  m_buffer = Buffer2();
  m_project = Project();
  if (p_error) {
    p_error->clear();
  }

  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return false;
  };
  if (!p_buffer.isValid() || p_buffer.isEncrypted() || p_buffer.nodeId().isVirtual()) {
    return fail(tr("Typst requires an available, unencrypted file buffer"));
  }
  const auto info = p_buffer.getBuffer();
  if (info.isEmpty() || info.value(QStringLiteral("isVirtual")).toBool()) {
    return fail(tr("Typst requires an available, unencrypted file buffer"));
  }

  const auto path = p_buffer.resolvedPath();
  const QFileInfo file(path);
  const auto name = file.fileName();
  if (path.isEmpty() || path.contains(QChar::Null) || !file.isAbsolute() || file.isDir() ||
      !isProjectPath(name)) {
    return fail(tr("Unable to resolve the Typst source file"));
  }
  const auto root = resolvedFileSystemPath(file.dir().absolutePath());
  if (root.isEmpty() || !QFileInfo(root).isDir()) {
    return fail(tr("The Typst project directory is unavailable"));
  }

  m_buffer = p_buffer;
  m_project.root = root;
  m_project.mainFile = name;
  return true;
}

const TypstViewWindowController::Project &TypstViewWindowController::getProject() const {
  return m_project;
}

bool TypstViewWindowController::bufferAvailable() const {
  return m_buffer.isValid() && !m_buffer.isEncrypted() && !m_project.root.isEmpty() &&
         !m_buffer.getBuffer().isEmpty();
}

NodeIdentifier TypstViewWindowController::resolveIndexedFile(const QString &p_absolutePath) const {
  auto *notebooks = m_services.get<NotebookCoreService>();
  const auto resolved = notebooks->resolvePathToNotebook(p_absolutePath);
  NodeIdentifier node;
  node.notebookId = resolved.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
  node.relativePath = resolved.value(QStringLiteral("relativePath")).toString();
  // Path resolution identifies a containing notebook, not necessarily an indexed note.
  if (!node.isValid() || node.relativePath.isEmpty() ||
      notebooks->getFileInfo(node.notebookId, node.relativePath).isEmpty()) {
    return {};
  }
  return node;
}

void TypstViewWindowController::requestOpenSource(const QString &p_file, int p_start, int p_end) {
  if (!bufferAvailable() || !isProjectPath(p_file) || p_start < 0 || p_end < p_start) {
    emit errorOccurred(tr("Unable to open the Typst source location"));
    return;
  }

  const QDir root(m_project.root);
  const QFileInfo file(root.filePath(p_file));
  const auto canonicalPath = resolvedFileSystemPath(file.absoluteFilePath());
  const auto relativePath = root.relativeFilePath(canonicalPath);
  if (!file.isFile() || canonicalPath.isEmpty() || QDir::isAbsolutePath(relativePath) ||
      relativePath == QLatin1String(".") || relativePath == QLatin1String("..") ||
      relativePath.startsWith(QLatin1String("../"))) {
    emit errorOccurred(tr("Unable to open the Typst source location"));
    return;
  }

  auto node = resolveIndexedFile(canonicalPath);
  if (!node.isValid()) {
    node.relativePath = canonicalPath;
  }
  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Edit;
  settings.m_forceMode = true;
  settings.m_cursorOffset = p_start;
  // BufferService owns hooks and adopts the target's existing/notebook read-only state.
  const auto generation = m_generation;
  const QPointer<TypstViewWindowController> guard(this);
  const auto opened = m_services.get<BufferService>()->openBuffer(node, settings);
  if (guard && generation == m_generation && !opened.isValid()) {
    emit errorOccurred(tr("Unable to open the Typst source location"));
  }
}

void TypstViewWindowController::requestOpenLink(const QUrl &p_url) {
  if (m_externalConfirmationActive || !bufferAvailable() || p_url.isEmpty() ||
      !m_pendingExternalLink.isEmpty()) {
    return;
  }
  if (!p_url.isValid() || containsNull(p_url)) {
    emit errorOccurred(tr("Unable to open an invalid link"));
    return;
  }
  if (externalSchemeAllowed(p_url)) {
    confirmExternalLink(p_url);
    return;
  }
  if ((!p_url.scheme().isEmpty() && !p_url.isLocalFile()) || !p_url.authority().isEmpty()) {
    emit errorOccurred(tr("This link is not allowed for this note"));
    return;
  }

  const auto path = p_url.isLocalFile() ? p_url.toLocalFile() : p_url.path(QUrl::FullyDecoded);
  if (!isLocalLinkPath(path)) {
    emit errorOccurred(tr("This link is not allowed for this note"));
    return;
  }
  const auto absolutePath = QDir::cleanPath(QDir(m_project.root).absoluteFilePath(path));
  const QFileInfo file(absolutePath);
  if (!file.isFile()) {
    emit errorOccurred(tr("The linked file does not exist"));
    return;
  }
  const auto canonicalPath = resolvedFileSystemPath(absolutePath);
  if (canonicalPath.isEmpty() || !isLocalLinkPath(canonicalPath)) {
    emit errorOccurred(tr("This link is not allowed for this note"));
    return;
  }

  const auto node = resolveIndexedFile(canonicalPath);
  if (!node.isValid()) {
    confirmExternalLink(QUrl::fromLocalFile(canonicalPath));
    return;
  }
  FileOpenSettings settings;
  settings.m_anchor = p_url.fragment(QUrl::FullyDecoded);
  const auto generation = m_generation;
  const QPointer<TypstViewWindowController> guard(this);
  const auto opened = m_services.get<BufferService>()->openBuffer(node, settings);
  if (guard && generation == m_generation && !opened.isValid()) {
    emit errorOccurred(tr("Unable to open the link"));
  }
}

void TypstViewWindowController::confirmExternalLink(const QUrl &p_url) {
  const auto url = p_url;
  m_pendingExternalLink = url;
  m_pendingExternalGeneration = m_generation;
  // Retargeting invalidates consent, but must not admit a new request while the old modal
  // response is on the stack: the public confirmation callback carries only the URL.
  m_externalConfirmationActive = true;
  const QPointer<TypstViewWindowController> guard(this);
  emit externalLinkConfirmationRequested(url);
  if (guard) {
    m_externalConfirmationActive = false;
  }
}

void TypstViewWindowController::openConfirmedExternalLink(const QUrl &p_url) {
  const auto pending = m_pendingExternalLink;
  const auto generation = m_pendingExternalGeneration;
  // Consume before comparing, so cancellation and mismatched callbacks cannot leave consent.
  m_pendingExternalLink = QUrl();
  m_pendingExternalGeneration = 0;
  if (pending.isEmpty() || pending != p_url || generation != m_generation || !bufferAvailable()) {
    return;
  }
  if (!p_url.isValid() || containsNull(p_url) ||
      (!externalSchemeAllowed(p_url) &&
       (!p_url.isLocalFile() || !p_url.authority().isEmpty() ||
        !isLocalLinkPath(p_url.toLocalFile()) || !QFileInfo(p_url.toLocalFile()).isAbsolute() ||
        !QFileInfo(p_url.toLocalFile()).isFile()))) {
    emit errorOccurred(tr("This link is not allowed for this note"));
    return;
  }
  const QPointer<TypstViewWindowController> guard(this);
  const bool opened = QDesktopServices::openUrl(p_url);
  if (guard && generation == m_generation && !opened) {
    emit errorOccurred(tr("Unable to open the link"));
  }
}
