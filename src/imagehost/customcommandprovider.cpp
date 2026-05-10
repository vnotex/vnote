#include "customcommandprovider.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

using namespace vnotex;

CustomCommandProvider::CustomCommandProvider(QObject *p_parent)
  : IImageHostProvider(p_parent)
{
}

QString CustomCommandProvider::typeId() const
{
  return QStringLiteral("custom_command");
}

QString CustomCommandProvider::displayName() const
{
  return tr("Custom Command");
}

ImageUploadResult CustomCommandProvider::upload(const QByteArray &p_data, const QString &p_path)
{
  Q_UNUSED(p_path);

  ImageUploadResult result;

  if (m_command.isEmpty()) {
    result.errorMessage = tr("Command is not configured");
    return result;
  }

  // Save image data to a temporary file.
  QTemporaryFile tempFile;
  tempFile.setAutoRemove(true);
  if (!tempFile.open()) {
    result.errorMessage = tr("Failed to create temporary file");
    return result;
  }
  tempFile.write(p_data);
  tempFile.close();

  const QString tempFilePath = tempFile.fileName();

  // Split command into program and arguments.
  QStringList parts = QProcess::splitCommand(m_command);
  if (parts.isEmpty()) {
    result.errorMessage = tr("Invalid command: %1").arg(m_command);
    return result;
  }

  const QString program = parts.takeFirst();
  parts.append(tempFilePath);

  // Execute the command.
  QProcess process;
  process.start(program, parts);

  if (!process.waitForStarted(5000)) {
    result.errorMessage = tr("Command not found: %1").arg(program);
    return result;
  }

  if (!process.waitForFinished(30000)) {
    process.kill();
    result.errorMessage = tr("Command timed out after 30 seconds");
    return result;
  }

  if (process.exitCode() != 0) {
    const QString stderrOutput = QString::fromUtf8(process.readAllStandardError()).trimmed();
    result.errorMessage = tr("Command failed (exit code %1): %2")
                            .arg(process.exitCode())
                            .arg(stderrOutput);
    return result;
  }

  // Parse stdout: take last non-empty line as uploaded URL.
  const QString stdoutOutput = QString::fromUtf8(process.readAllStandardOutput());
  const QStringList lines = stdoutOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

  QString url;
  for (int i = lines.size() - 1; i >= 0; --i) {
    const QString line = lines[i].trimmed();
    if (!line.isEmpty()) {
      url = line;
      break;
    }
  }

  if (url.isEmpty()) {
    result.errorMessage = tr("No URL returned by command");
    return result;
  }

  result.success = true;
  result.imageUrl = url;
  result.fileName = QFileInfo(tempFilePath).fileName();
  return result;
}

bool CustomCommandProvider::supportsDelete() const
{
  return false;
}

bool CustomCommandProvider::remove(const QString &p_url, QString &p_msg)
{
  Q_UNUSED(p_url);
  p_msg = tr("Delete is not supported by custom command provider");
  return false;
}

bool CustomCommandProvider::ownsUrl(const QString &p_url) const
{
  Q_UNUSED(p_url);
  return false;
}

QJsonObject CustomCommandProvider::getConfig() const
{
  QJsonObject obj;
  obj[QStringLiteral("command")] = m_command;
  return obj;
}

void CustomCommandProvider::setConfig(const QJsonObject &p_config)
{
  m_command = p_config.value(QStringLiteral("command")).toString();
}

bool CustomCommandProvider::testConfig(const QJsonObject &p_config, QString &p_msg)
{
  const QString command = p_config.value(QStringLiteral("command")).toString();
  if (command.isEmpty()) {
    p_msg = tr("Command is empty");
    return false;
  }

  // Optionally check if the program exists.
  QStringList parts = QProcess::splitCommand(command);
  if (parts.isEmpty()) {
    p_msg = tr("Invalid command: %1").arg(command);
    return false;
  }

  const QString program = parts.first();
  const QString resolved = QStandardPaths::findExecutable(program);
  if (resolved.isEmpty()) {
    p_msg = tr("Program not found in PATH: %1").arg(program);
    return false;
  }

  p_msg = tr("Configuration is valid. Program found: %1").arg(resolved);
  return true;
}

bool CustomCommandProvider::ready() const
{
  return !m_command.isEmpty();
}
