#ifndef TASK_H
#define TASK_H

#include <QObject>
#include <QJsonObject>
#include <QVector>
#include <QMap>
#include <QProcess>

class QAction;

namespace vnotex {

    class Task : public QObject
    {
        Q_OBJECT
    public:
        
        struct Input {
            QString m_id;
            QString m_type;
            QString m_description;
            QString m_default;
            bool m_password;
            QStringList m_options;
        };
        
        static Task* fromFile(const QString &p_file, 
                                    const QString &p_locale,
                                    QObject *p_parent = nullptr);
        
        QString getVersion() const;
        
        QString getType() const;
        
        QString getCommand() const;
        
        QStringList getArgs() const;
        
        QString getLabel() const;
        
        QString getOptionsCwd() const;
        
        const QMap<QString, QString> &getOptionsEnv() const;
        
        QString getShell() const;
        
        QString getOptionsShellExecutable() const;
        
        QStringList getOptionsShellArgs() const;
        
        const QVector<Task*> &getTasks() const;
        
        const QVector<Input> &getInputs() const;
        
        Input getInput(const QString &p_id) const;
        
        QString getFile() const;
        
        static QAction *runAction(Task *p_task);
        
        static QString s_latestVersion;
        
        static bool isValidTaskFile(const QString &p_file);
        
        static QString getLocaleString(const QJsonValue &p_value,
                                       const QString &p_locale);
        
        static QStringList getLocaleStringList(const QJsonValue &p_value,
                                          const QString &p_locale);
        
        static QStringList getStringList(const QJsonValue &p_value);
        
    signals:
        void showOutput(const QString &p_text) const;
        
    private:
        static Task* fromJson(Task *p_task,
                              const QJsonObject &p_obj);
        
        static Task* fromJsonV0(Task *p_task,
                                const QJsonObject &p_obj,
                                bool mergeTasks = false);        
        
        explicit Task(const QString &p_locale, 
                            const QString &p_file = QString(), 
                            QObject *p_parent = nullptr);
        
        QProcess *setupProcess() const;
        
        void run() const;
        
        QString replaceVariables(const QString &p_text) const;
        
        QStringList replaceVariables(const QStringList &p_list) const;
        
        QStringList getAllInputVariables(const QString &p_text) const;
        
        QMap<QString, QString> evaluateInputVariables(const QString &p_text) const;
        
        QString replaceInputVariables(const QString &p_text) const;
        
        static QString textDecode(const QByteArray &p_text);
        
        static QString textDecode(const QByteArray &p_text, const QByteArray &name);
        
        static QJsonObject readTaskFile(const QString &p_file);
        
        static QStringList defaultShellArgs(const QString &p_shell);
        
        /**
         * windows use \
         * others use /
         */
        static QString normalPath(const QString &p_path);
        
        /**
         * if p_text contain <space>
         * Wrap it in @p_chars.
         */
        static QString spaceQuote(const QString &p_text, const QString &p_chars = "\"");
        
        static QStringList spaceQuote(const QStringList &p_list, const QString &p_chars = "\"");
        
        static QString getCurrentFile();
        
        static QString getCurrentNotebookFolder();
        
        
        QString m_version;
        
        QString m_type;
        
        QString m_command;
        
        QStringList m_args;
        
        QString m_label;
        
        QString m_options_cwd;
        
        QMap<QString, QString> m_options_env;
        
        QString m_options_shell_executable;
        
        QStringList m_options_shell_args;
        
        QVector<Task*> m_tasks;
        
        QVector<Input> m_inputs;
        
        Task *m_parent = nullptr;
        
        QString m_file;
        
        QString m_locale;
    };

} // ns vnotex

#endif // TASK_H
