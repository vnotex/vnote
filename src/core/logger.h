#ifndef LOGGER_H
#define LOGGER_H

#include <QMessageLogContext>
#include <QString>

class QFile;

namespace vnotex {
class Logger {
public:
  Logger() = delete;

  static void init(bool p_verbose, bool p_logToStderr);

private:
  static void log(QtMsgType p_type, const QMessageLogContext &p_context, const QString &p_msg);

  static QFile s_file;

  static bool s_verbose;

  static bool s_logToStderr;
};
} // namespace vnotex

#endif // LOGGER_H
