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

        static void init(bool p_debugLog);

    private:
        static void log(QtMsgType p_type, const QMessageLogContext &p_context, const QString &p_msg);

        static QFile s_file;

        static bool s_debugLog;
    };
}

#endif // LOGGER_H
