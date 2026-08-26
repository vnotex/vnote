#include "autostartutils.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#if defined(Q_OS_WIN)
#include <QSettings>
#endif

using namespace vnotex;

#if defined(Q_OS_WIN)
namespace {
const QString c_runKey =
    QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString c_valueName = QStringLiteral("VNote");
} // namespace
#endif

bool AutoStartUtils::isSupported() {
#if defined(Q_OS_WIN)
  return true;
#else
  return false;
#endif
}

QString AutoStartUtils::expectedCommand() {
  // Computed on every platform so that decide() and its tests are platform-agnostic.
  return QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
         QLatin1Char('"');
}

AutoStartAction AutoStartUtils::decide(bool p_intent, bool p_present, const QString &p_current,
                                       const QString &p_expected) {
  if (p_intent) {
    if (p_present && p_current == p_expected) {
      return AutoStartAction::None;
    }
    return AutoStartAction::Write;
  }

  return p_present ? AutoStartAction::Remove : AutoStartAction::None;
}

bool AutoStartUtils::hasRegisteredCommand() {
#if defined(Q_OS_WIN)
  QSettings reg(c_runKey, QSettings::NativeFormat);
  return reg.contains(c_valueName);
#else
  return false;
#endif
}

QString AutoStartUtils::registeredCommand() {
#if defined(Q_OS_WIN)
  QSettings reg(c_runKey, QSettings::NativeFormat);
  return reg.value(c_valueName).toString();
#else
  return QString();
#endif
}

bool AutoStartUtils::setEnabled(bool p_enabled) {
#if defined(Q_OS_WIN)
  QSettings reg(c_runKey, QSettings::NativeFormat);
  if (p_enabled) {
    reg.setValue(c_valueName, expectedCommand());
  } else {
    reg.remove(c_valueName);
  }
  reg.sync();
  return reg.status() == QSettings::NoError;
#else
  Q_UNUSED(p_enabled);
  return false;
#endif
}

bool AutoStartUtils::reconcile(bool p_intent) {
#if defined(Q_OS_WIN)
  const auto action =
      decide(p_intent, hasRegisteredCommand(), registeredCommand(), expectedCommand());
  switch (action) {
  case AutoStartAction::None:
    return true;

  case AutoStartAction::Write:
    if (!setEnabled(true)) {
      qWarning() << "failed to write the Windows startup entry";
      return false;
    }
    qInfo() << "wrote the Windows startup entry";
    return true;

  case AutoStartAction::Remove:
    if (!setEnabled(false)) {
      qWarning() << "failed to remove the Windows startup entry";
      return false;
    }
    qInfo() << "removed the Windows startup entry";
    return true;
  }

  return true;
#else
  Q_UNUSED(p_intent);
  return true;
#endif
}
