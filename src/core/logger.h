#ifndef LOGGER_H
#define LOGGER_H

#include <QMessageLogContext>
#include <QMutex>
#include <QString>
#include <QVector>

class QFile;

namespace vnotex {
class Logger {
public:
  Logger() = delete;

  // Install the Qt message handler at the very beginning of main(), before the
  // logger state (verbose/quiet/stderr) and the log file path are known. Every
  // message is captured into an in-memory buffer instead of leaking to Qt's
  // default handler (which would otherwise print early startup logs, e.g.
  // vxcore context creation, to the console).
  static void installEarly();

  // Resolve the final logger state once the command line and config paths are
  // available, then flush the buffered messages through the normal filters
  // (dropping any below the active threshold) and switch to live logging.
  static void configure(const QString &p_logFilePath, bool p_verbose, bool p_logToStderr,
                        bool p_quiet);

private:
  struct BufferedMessage {
    QtMsgType m_type;
    QString m_fileName;
    int m_line;
    QString m_msg;
  };

  static void log(QtMsgType p_type, const QMessageLogContext &p_context, const QString &p_msg);

  // Apply the active verbose/quiet filters and write to the resolved sink.
  // The caller must hold s_mutex.
  static void emitMessage(QtMsgType p_type, const QString &p_fileName, int p_line,
                          const QString &p_msg);

  static void writeToFile(QtMsgType p_type, const QString &p_fileName, int p_line,
                          const QString &p_msg);

  static void writeToStderr(QtMsgType p_type, const QString &p_fileName, int p_line,
                            const QString &p_msg);

  static QString header(QtMsgType p_type);

  static QFile s_file;

  static bool s_verbose;

  static bool s_logToStderr;

  static bool s_quiet;

  // Whether configure() has run. Before that, messages are buffered.
  static bool s_configured;

  // Messages captured before configure() resolves the final logger state.
  static QVector<BufferedMessage> s_buffer;

  // Number of pre-configure messages dropped because the buffer hit its cap.
  static int s_droppedCount;

  // Serializes handler state transitions and sink writes. The Qt message
  // handler may be invoked from multiple threads.
  static QMutex s_mutex;
};
} // namespace vnotex

#endif // LOGGER_H
