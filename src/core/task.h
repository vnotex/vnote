#ifndef TASK_H
#define TASK_H

#include <QObject>
#include <QJsonObject>
#include <QVector>
#include <QMap>
#include <QProcess>

#include "taskvariablemgr.h"

class QAction;

namespace vnotex {

    struct ButtonDTO {
        QString text;
    };

    struct InputDTO {
        QString id;

        QString type;

        QString description;

        QString default_;

        bool password;

        QStringList options;
    };

    struct MessageDTO {
        QString id;

        QString type;

        QString title;

        QString text;

        QString detailedText;

        QVector<ButtonDTO> buttons;
    };

    struct ShellOptionsDTO {
        QString executable;

        QStringList args;
    };

    struct TaskOptionsDTO {
        QString cwd;

        QMap<QString, QString> env;

        ShellOptionsDTO shell;
    };

    struct TaskDTO {
        QString version;

        QString type;

        QString command;

        QStringList args;

        QString label;

        QString icon;

        QString shortcut;

        QVector<InputDTO> inputs;

        QVector<MessageDTO> messages;

        TaskOptionsDTO options;

        QString _scope;

        QString _source;
    };

    class Notebook;

    class Task : public QObject
    {
        Q_OBJECT
    public:


        static Task* fromFile(const QString &p_file,
                              const QJsonDocument &p_json,
                              const QString &p_locale,
                              QObject *p_parent = nullptr);

        void run() const;

        TaskDTO getDTO() const;

        QString getVersion() const;

        QString getType() const;

        QString getCommand() const;

        QStringList getArgs() const;

        QString getLabel() const;

        QString getIcon() const;

        QString getShortcut() const;

        QString getOptionsCwd() const;

        const QMap<QString, QString> &getOptionsEnv() const;

        QString getOptionsShellExecutable() const;

        QStringList getOptionsShellArgs() const;

        const QVector<Task*> &getTasks() const;

        const QVector<InputDTO> &getInputs() const;

        InputDTO getInput(const QString &p_id) const;

        MessageDTO getMessage(const QString &p_id) const;

        QString getFile() const;

        static QString s_latestVersion;

        static bool isValidTaskFile(const QString &p_file,
                                    QJsonDocument &p_json);

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

        static QString textDecode(const QByteArray &p_text);

        static QString textDecode(const QByteArray &p_text, const QByteArray &name);

        TaskDTO dto;

        Task *m_parent = nullptr;

        QVector<Task*> m_tasks;

        QString m_locale;

        static TaskVariableMgr s_vars;

    };

} // ns vnotex

#endif // TASK_H
