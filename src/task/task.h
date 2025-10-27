#ifndef TASK_H
#define TASK_H

#include <QObject>

#include <QMap>
#include <QSharedPointer>
#include <QVector>

class QAction;
class QProcess;
class QJsonObject;

namespace tests {
class TestTask;
}

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

class TaskMgr;
class TaskVariableMgr;

class Task : public QObject {
  Q_OBJECT
public:
  friend class tests::TestTask;

  // For top level Task, use QSharedPointer instead of QObject to manage ownership.
  static QSharedPointer<Task> fromFile(const QString &p_file, const QString &p_locale,
                                       TaskMgr *p_taskMgr);

  void run();

  const TaskDTO &getDTO() const;

  const QString &getVersion() const;

  const QString &getType() const;

  QString getCommand();

  QStringList getArgs();

  const QString &getLabel() const;

  const QString &getIcon() const;

  const QString &getShortcut() const;

  QString getOptionsCwd();

  const QMap<QString, QString> &getOptionsEnv() const;

  const QString &getOptionsShellExecutable() const;

  QStringList getOptionsShellArgs();

  const QVector<Task *> &getChildren() const;

  const QVector<InputDTO> &getInputs() const;

  const InputDTO *findInput(const QString &p_id) const;

  const MessageDTO *findMessage(const QString &p_id) const;

  const QString &getFile() const;

  bool isCancelled() const;

  void setCancelled(bool p_cancelled);

  static QString s_latestVersion;

  static QString getLocaleString(const QJsonValue &p_value, const QString &p_locale);

  static QStringList getLocaleStringList(const QJsonValue &p_value, const QString &p_locale);

  static QStringList getStringList(const QJsonValue &p_value);

  static QString decodeText(const QByteArray &p_text);

signals:
  void outputRequested(const QString &p_text) const;

private:
  Task(const QString &p_locale, const QString &p_file, TaskMgr *p_taskMgr,
       QObject *p_parent = nullptr);

  // Must call start() or delete the returned QProcess.
  QProcess *setupProcess();

  const TaskVariableMgr &variableMgr() const;

  static bool fromJson(Task *p_task, const QJsonObject &p_obj);

  static bool fromJsonV0(Task *p_task, const QJsonObject &p_obj, bool p_mergeTasks = false);

  static QString decodeText(const QByteArray &p_text, const QByteArray &p_name);

  Task *m_parent = nullptr;

  QVector<Task *> m_children;

  TaskMgr *m_taskMgr = nullptr;

  TaskDTO m_dto;

  QString m_locale;

  bool m_cancelled = false;
};
} // namespace vnotex

#endif // TASK_H
