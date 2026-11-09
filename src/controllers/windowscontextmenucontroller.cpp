#include "windowscontextmenucontroller.h"

#ifdef Q_OS_WIN
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <windows.h>

#include <core/servicelocator.h>
#include <core/services/filetypecoreservice.h>

using namespace vnotex;

WindowsContextMenuController::WindowsContextMenuController(ServiceLocator &p_services)
    : QObject(QCoreApplication::instance()), m_services(p_services) {
  Q_ASSERT(QCoreApplication::instance());
  Q_ASSERT(!p_services.get<WindowsContextMenuController>());
  p_services.registerService(this);

  m_process.setProcessChannelMode(QProcess::MergedChannels);
  m_process.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments *p_args) { p_args->flags |= CREATE_NO_WINDOW; });
  connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError p_error) {
    if (p_error == QProcess::FailedToStart) {
      complete(false, tr("Failed to start Windows PowerShell: %1").arg(m_process.errorString()));
    }
  });
  connect(
      &m_process, &QProcess::finished, this,
      [this](int p_exitCode, QProcess::ExitStatus p_exitStatus) {
        if (p_exitStatus == QProcess::NormalExit && p_exitCode == 0) {
          complete(true, m_action == Action::Add
                             ? tr("Added the VNote actions to the system context menu.")
                             : tr("Removed the VNote actions from the system context menu."));
          return;
        }

        auto message =
            p_exitStatus == QProcess::CrashExit
                ? tr("The context menu helper stopped unexpectedly.")
                : tr("Failed to update the system context menu (exit code %1).").arg(p_exitCode);
        const auto details = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
        if (!details.isEmpty()) {
          message += QLatin1Char('\n') + details;
        }
        complete(false, message);
      });
  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
          &WindowsContextMenuController::shutdown);
}

WindowsContextMenuController::~WindowsContextMenuController() { shutdown(); }

bool WindowsContextMenuController::start(Action p_action) {
  if (m_busy || m_shuttingDown) {
    return false;
  }

  m_action = p_action;
  m_busy = true;
  m_error = false;
  m_statusText = p_action == Action::Add ? tr("Adding the VNote context menu actions...")
                                         : tr("Removing the VNote context menu actions...");
  emit stateChanged();

  QStringList suffixes;
  if (p_action == Action::Add) {
    const auto *fileTypes = m_services.get<FileTypeCoreService>();
    const auto types = fileTypes ? fileTypes->getAllFileTypes() : QVector<FileType>();
    if (types.isEmpty()) {
      complete(false, tr("The built-in file types are unavailable."));
      return false;
    }
    static const QRegularExpression invalidSuffix(QStringLiteral(R"([.\s\\/:*?"<>|;\x00-\x1f])"),
                                                  QRegularExpression::UseUnicodePropertiesOption);
    for (const auto &type : types) {
      if (type.m_typeName == QLatin1String("Others")) {
        continue;
      }
      for (const auto &configuredSuffix : type.m_suffixes) {
        auto suffix = configuredSuffix.trimmed().toLower();
        if (suffix.isEmpty()) {
          continue;
        }
        if (suffix.startsWith(QLatin1Char('.'))) {
          suffix.remove(0, 1);
        }
        // Validate individual values before joining: a stored ';' must not add another suffix.
        if (suffix.isEmpty() || suffix.size() > 254 || invalidSuffix.match(suffix).hasMatch()) {
          complete(false, tr("A built-in file suffix is invalid for the Windows context menu: %1")
                              .arg(configuredSuffix));
          return false;
        }
        suffixes.append(suffix);
      }
    }
    suffixes.removeDuplicates();
  }

  const auto helperPath = QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("windows-context-menu.ps1"));
  if (!QFileInfo(helperPath).isFile()) {
    complete(false, tr("The installed context menu helper is missing: %1")
                        .arg(QDir::toNativeSeparators(helperPath)));
    return false;
  }

  wchar_t systemDirectory[MAX_PATH + 1];
  const UINT systemDirectorySize = GetSystemDirectoryW(systemDirectory, MAX_PATH + 1);
  if (systemDirectorySize == 0 || systemDirectorySize > MAX_PATH) {
    complete(false, tr("Could not locate the Windows system directory."));
    return false;
  }
  const auto program =
      QDir(QString::fromWCharArray(systemDirectory, static_cast<int>(systemDirectorySize)))
          .filePath(QStringLiteral("WindowsPowerShell/v1.0/powershell.exe"));
  if (!QFileInfo(program).isFile()) {
    complete(false,
             tr("Windows PowerShell is unavailable at %1.").arg(QDir::toNativeSeparators(program)));
    return false;
  }

  m_process.setWorkingDirectory(QCoreApplication::applicationDirPath());
  QStringList arguments{QStringLiteral("-NoProfile"),
                        QStringLiteral("-NonInteractive"),
                        QStringLiteral("-ExecutionPolicy"),
                        QStringLiteral("Bypass"),
                        QStringLiteral("-File"),
                        QDir::toNativeSeparators(helperPath),
                        QStringLiteral("-Action"),
                        p_action == Action::Add ? QStringLiteral("Add") : QStringLiteral("Remove"),
                        QStringLiteral("-VNotePath"),
                        QDir::toNativeSeparators(QCoreApplication::applicationFilePath())};
  if (p_action == Action::Add) {
    arguments << QStringLiteral("-Suffixes") << suffixes.join(QLatin1Char(';'));
  }
  m_process.start(program, arguments);
  return true;
}

void WindowsContextMenuController::complete(bool p_success, const QString &p_message) {
  if (!m_busy || m_shuttingDown) {
    return;
  }
  m_busy = false;
  m_error = !p_success;
  m_statusText = p_message;
  emit stateChanged();
}

void WindowsContextMenuController::shutdown() {
  if (m_shuttingDown) {
    return;
  }
  m_shuttingDown = true;
  disconnect(&m_process, nullptr, this, nullptr);
  // Closing Settings never interrupts a mutation. At application shutdown allow
  // a short grace period, then explicitly reap the helper without an unbounded wait.
  if (m_process.state() != QProcess::NotRunning && !m_process.waitForFinished(3000)) {
    m_process.kill();
    m_process.waitForFinished(1000);
  }
}
#endif // Q_OS_WIN
