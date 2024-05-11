#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QMessageLogContext>

class QFile;

namespace vnotex
{
    class Logger
    {
    public:
        Logger() = delete;

        static void init(bool p_verbose, bool p_logToStderr);

    private:
        static void log(QtMsgType p_type, const QMessageLogContext &p_context, const QString &p_msg);

        static QFile s_file;

        static bool s_verbose;

        static bool s_logToStderr;
    };
}

#endif // LOGGER_H
