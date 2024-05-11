#include "logger.h"

#include <QFile>
#include <QTextStream>
#include "configmgr.h"

using namespace vnotex;

QFile Logger::s_file;

bool Logger::s_verbose = false;

bool Logger::s_logToStderr = false;

void Logger::init(bool p_verbose, bool p_logToStderr)
{
    s_verbose = p_verbose;
    s_logToStderr = p_logToStderr;

#if defined(QT_NO_DEBUG)
    if (!s_logToStderr) {
        s_file.setFileName(ConfigMgr::getInst().getLogFile());
        if (s_file.size() >= 5 * 1024 * 1024) {
            s_file.open(QIODevice::WriteOnly | QIODevice::Text);
        } else {
            s_file.open(QIODevice::Append | QIODevice::Text);
        }
    }
#else
    // Always log to stderr in debug.
    s_logToStderr = true;
#endif

    qInstallMessageHandler(Logger::log);
}

static QString getFileName(const char *p_file)
{
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

void Logger::log(QtMsgType p_type, const QMessageLogContext &p_context, const QString &p_msg)
{
#if defined(QT_NO_DEBUG)
    if (!s_verbose && p_type == QtDebugMsg) {
        return;
    }
#endif

    QByteArray localMsg = p_msg.toUtf8();
    QString header;

    switch (p_type) {
    case QtDebugMsg:
        header = QStringLiteral("Debug:");
        break;

    case QtInfoMsg:
        header = QStringLiteral("Info:");
        break;

    case QtWarningMsg:
        header = QStringLiteral("Warning:");
        break;

    case QtCriticalMsg:
        header = QStringLiteral("Critical:");
        break;

    case QtFatalMsg:
        header = QStringLiteral("Fatal:");
        break;
    }

    QString fileName = getFileName(p_context.file);

    if (!s_logToStderr) {
        QTextStream stream(&s_file);
        stream << header << (QString("(%1:%2) ").arg(fileName).arg(p_context.line))
               << localMsg << "\n";

        if (p_type == QtFatalMsg) {
            s_file.close();
            abort();
        }
    } else {
        std::string fileStr = fileName.toStdString();
        const char *file = fileStr.c_str();

        switch (p_type) {
        case QtDebugMsg:
            fprintf(stderr, "%s(%s:%u) %s\n",
                    header.toStdString().c_str(), file, p_context.line, localMsg.constData());
            break;
        case QtInfoMsg:
            fprintf(stderr, "%s(%s:%u) %s\n",
                    header.toStdString().c_str(), file, p_context.line, localMsg.constData());
            break;
        case QtWarningMsg:
            fprintf(stderr, "%s(%s:%u) %s\n",
                    header.toStdString().c_str(), file, p_context.line, localMsg.constData());
            break;
        case QtCriticalMsg:
            fprintf(stderr, "%s(%s:%u) %s\n",
                    header.toStdString().c_str(), file, p_context.line, localMsg.constData());
            break;
        case QtFatalMsg:
            fprintf(stderr, "%s(%s:%u) %s\n",
                    header.toStdString().c_str(), file, p_context.line, localMsg.constData());
            abort();
            break;
        }

        fflush(stderr);
    }
}
