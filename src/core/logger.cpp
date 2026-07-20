#include "logger.h"

#include <QFile>
#include <QMutexLocker>
#include <QTextStream>

using namespace vnotex;

QFile Logger::s_file;

bool Logger::s_verbose = false;

bool Logger::s_logToStderr = false;

bool Logger::s_quiet = false;

bool Logger::s_configured = false;

QVector<Logger::BufferedMessage> Logger::s_buffer;

int Logger::s_droppedCount = 0;

QMutex Logger::s_mutex;

// Upper bound on messages buffered before configure() resolves the logger
// state. In practice configure() runs early during startup, so this is only a
// safety valve against a stalled or flood-prone startup path growing the buffer
// without bound. Excess messages are dropped and summarized at flush time.
static constexpr int kMaxBufferedMessages = 4096;

void Logger::installEarly() {
  qInstallMessageHandler(Logger::log);
}

void Logger::configure(const QString &p_logFilePath, bool p_verbose, bool p_logToStderr,
                       bool p_quiet) {
  QMutexLocker locker(&s_mutex);

  s_verbose = p_verbose;
  s_logToStderr = p_logToStderr;
  s_quiet = p_quiet;

#if defined(QT_NO_DEBUG)
  if (!s_logToStderr) {
    s_file.setFileName(p_logFilePath);
    const bool opened = (s_file.size() >= 5 * 1024 * 1024)
                            ? s_file.open(QIODevice::WriteOnly | QIODevice::Text)
                            : s_file.open(QIODevice::Append | QIODevice::Text);
    if (!opened) {
      // The log file could not be opened (unwritable dir, permissions, bad
      // path). Fall back to stderr so we never write through QTextStream onto a
      // closed QFile: that emits a Qt qWarning which would re-enter this handler
      // and self-deadlock on the non-recursive s_mutex we hold here.
      s_logToStderr = true;
    }
  }
#else
  // Always log to stderr in debug.
  s_logToStderr = true;
  Q_UNUSED(p_logFilePath);
#endif

  // Flush buffered messages through the normal filters now that the sink and
  // thresholds are known. Below-threshold messages are dropped here. This runs
  // BEFORE marking the logger configured so no live message can jump ahead of
  // the buffered ones (both paths hold s_mutex).
  for (const auto &msg : qAsConst(s_buffer)) {
    emitMessage(msg.m_type, msg.m_fileName, msg.m_line, msg.m_msg);
  }
  s_buffer.clear();

  if (s_droppedCount > 0) {
    emitMessage(QtWarningMsg, QStringLiteral("logger.cpp"), 0,
                QStringLiteral("%1 early startup log message(s) dropped before logger "
                               "configuration (buffer cap reached).")
                    .arg(s_droppedCount));
    s_droppedCount = 0;
  }

  s_configured = true;
}

static QString getFileName(const char *p_file) {
  QString file(p_file);
  int idx = file.lastIndexOf(QChar('/'));
  if (idx == -1) {
    idx = file.lastIndexOf(QChar('\\'));
  }

  if (idx == -1) {
    return file;
  } else {
    return file.mid(idx + 1);
  }
}

QString Logger::header(QtMsgType p_type) {
  switch (p_type) {
  case QtDebugMsg:
    return QStringLiteral("Debug:");
  case QtInfoMsg:
    return QStringLiteral("Info:");
  case QtWarningMsg:
    return QStringLiteral("Warning:");
  case QtCriticalMsg:
    return QStringLiteral("Critical:");
  case QtFatalMsg:
    return QStringLiteral("Fatal:");
  }
  return QString();
}

void Logger::writeToFile(QtMsgType p_type, const QString &p_fileName, int p_line,
                         const QString &p_msg) {
  QTextStream stream(&s_file);
  stream << header(p_type) << (QStringLiteral("(%1:%2) ").arg(p_fileName).arg(p_line)) << p_msg
         << "\n";

  if (p_type == QtFatalMsg) {
    s_file.close();
    abort();
  }
}

void Logger::writeToStderr(QtMsgType p_type, const QString &p_fileName, int p_line,
                           const QString &p_msg) {
  const QByteArray localMsg = p_msg.toUtf8();
  const std::string fileStr = p_fileName.toStdString();
  const char *file = fileStr.c_str();

  fprintf(stderr, "%s(%s:%u) %s\n", header(p_type).toStdString().c_str(), file,
          static_cast<unsigned>(p_line), localMsg.constData());
  fflush(stderr);

  if (p_type == QtFatalMsg) {
    abort();
  }
}

void Logger::emitMessage(QtMsgType p_type, const QString &p_fileName, int p_line,
                         const QString &p_msg) {
  // Quiet mode: drop non-critical messages. QtFatalMsg still falls through so
  // the abort() path runs.
  if (s_quiet && p_type != QtCriticalMsg && p_type != QtFatalMsg) {
    return;
  }

#if defined(QT_NO_DEBUG)
  if (!s_verbose && p_type == QtDebugMsg) {
    return;
  }
#endif

  if (s_logToStderr) {
    writeToStderr(p_type, p_fileName, p_line, p_msg);
  } else {
    writeToFile(p_type, p_fileName, p_line, p_msg);
  }
}

void Logger::log(QtMsgType p_type, const QMessageLogContext &p_context, const QString &p_msg) {
  const QString fileName = getFileName(p_context.file);

  QMutexLocker locker(&s_mutex);

  if (!s_configured) {
    // Not configured yet: buffer the message so we can decide later whether to
    // emit or drop it. A fatal message cannot wait for configure(), so surface
    // it to stderr immediately and abort.
    if (p_type == QtFatalMsg) {
      writeToStderr(p_type, fileName, p_context.line, p_msg);
      return;
    }
    if (s_buffer.size() >= kMaxBufferedMessages) {
      // Safety valve: don't grow without bound if configure() is delayed.
      ++s_droppedCount;
      return;
    }
    s_buffer.append({p_type, fileName, p_context.line, p_msg});
    return;
  }

  emitMessage(p_type, fileName, p_context.line, p_msg);
}
