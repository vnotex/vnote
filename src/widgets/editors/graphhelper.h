#ifndef GRAPHHELPER_H
#define GRAPHHELPER_H

#include <QProcess>
#include <QStringList>
#include <QPair>
#include <QQueue>

#include <core/noncopyable.h>
#include <core/global.h>
#include <vtextedit/lrucache.h>

namespace vnotex
{
    class GraphHelper : private Noncopyable
    {
    public:
        typedef std::function<void(quint64, TimeStamp, const QString &, const QString &)> ResultCallback;

        GraphHelper();

        void process(quint64 p_id,
                     TimeStamp p_timeStamp,
                     const QString &p_format,
                     const QString &p_text,
                     const ResultCallback &p_callback);

    protected:
        virtual QStringList getFormatArgs(const QString &p_format) = 0;

        void clearCache();

        void checkValidProgram();

        static QStringList getArgsToUse(const QStringList &p_args);

        static QString getCommandToUse(const QString &p_command,
                                       const QString &p_format);

        QString m_program;

        QStringList m_args;

        // If this is not empty, @m_program and @m_args will be ignored.
        QString m_overriddenCommand;

    private:
        struct Task
        {
            quint64 m_id = 0;

            TimeStamp m_timeStamp = 0;

            QString m_format;

            QString m_text;

            ResultCallback m_callback;
        };

        struct CacheItem
        {
            bool isNull() const
            {
                return m_data.isNull();
            }

            QString m_format;

            QString m_data;
        };

        void processOneTask();

        void finishOneTask(QProcess *p_process, int p_exitCode, QProcess::ExitStatus p_exitStatus);

        void finishOneTask(const QString &p_data);

        QQueue<Task> m_tasks;

        bool m_taskOngoing = false;

        // {text} -> CacheItem.
        vte::LruCache<QString, CacheItem> m_cache;

        // Whether @m_program is valid.
        bool m_programValid = true;
    };
}

#endif // GRAPHHELPER_H
