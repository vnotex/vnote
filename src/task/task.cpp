#include "task.h"

#include <QAction>
#include <QDebug>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QTextCodec>
#include <QVersionNumber>

#include <buffer/buffer.h>
#include <core/exception.h>
#include <core/vnotex.h>
#include <notebook/notebook.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>

#include "shellexecution.h"
#include "taskmgr.h"

using namespace vnotex;

QString Task::s_latestVersion = "0.1.3";

QSharedPointer<Task> Task::fromFile(const QString &p_file, const QString &p_locale,
                                    TaskMgr *p_taskMgr) {
  QSharedPointer<Task> task(new Task(p_locale, p_file, p_taskMgr, nullptr));
  const auto obj = FileUtils::readJsonFile(p_file);
  if (fromJson(task.data(), obj)) {
    return task;
  }
  return nullptr;
}

bool Task::fromJson(Task *p_task, const QJsonObject &p_obj) {
  // For child task, it will inherit the version from parent.
  if (p_obj.contains("version")) {
    p_task->m_dto.version = p_obj["version"].toString();
  }

  const auto version = QVersionNumber::fromString(p_task->getVersion());
  if (version.isNull()) {
    qWarning() << "invalid task" << p_task->m_dto._source;
    return false;
  }

  if (version < QVersionNumber(1, 0, 0)) {
    return fromJsonV0(p_task, p_obj);
  } else {
    qWarning() << "unknown task version" << version << p_task->m_dto._source;
    return false;
  }
}

bool Task::fromJsonV0(Task *p_task, const QJsonObject &p_obj, bool p_mergeTasks) {
  if (p_obj.contains("type")) {
    p_task->m_dto.type = p_obj["type"].toString();
  }

  if (p_obj.contains("icon")) {
    QString iconPath = p_obj["icon"].toString();
    if (!iconPath.isEmpty()) {
      if (QDir::isRelativePath(iconPath)) {
        iconPath = QFileInfo(p_task->m_dto._source).dir().absoluteFilePath(iconPath);
      }

      if (QFileInfo::exists(iconPath)) {
        p_task->m_dto.icon = iconPath;
      } else {
        qWarning() << "task icon does not exist" << p_task->getLabel() << iconPath;
      }
    }
  }

  if (p_obj.contains("shortcut")) {
    p_task->m_dto.shortcut = p_obj["shortcut"].toString();
  }

  if (p_obj.contains("type")) {
    p_task->m_dto.type = p_obj["type"].toString();
  }

  if (p_obj.contains("command")) {
    p_task->m_dto.command = getLocaleString(p_obj["command"], p_task->m_locale);
  }

  if (p_obj.contains("args")) {
    p_task->m_dto.args = getLocaleStringList(p_obj["args"], p_task->m_locale);
  }

  if (p_obj.contains("label")) {
    p_task->m_dto.label = getLocaleString(p_obj["label"], p_task->m_locale);
  } else if (p_task->m_dto.label.isNull() && !p_task->m_dto.command.isNull()) {
    p_task->m_dto.label = p_task->m_dto.command;
  }

  if (p_obj.contains("options")) {
    auto options = p_obj["options"].toObject();

    if (options.contains("cwd")) {
      p_task->m_dto.options.cwd = options["cwd"].toString();
    }

    if (options.contains("env")) {
      p_task->m_dto.options.env.clear();
      auto env = options["env"].toObject();
      for (auto it = env.begin(); it != env.end(); it++) {
        auto value = getLocaleString(it.value(), p_task->m_locale);
        p_task->m_dto.options.env.insert(it.key(), value);
      }
    }

    if (options.contains("shell") && p_task->getType() == "shell") {
      auto shell = options["shell"].toObject();

      if (shell.contains("executable")) {
        p_task->m_dto.options.shell.executable = shell["executable"].toString();
      }

      if (shell.contains("args")) {
        p_task->m_dto.options.shell.args.clear();

        const auto arr = shell["args"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
          p_task->m_dto.options.shell.args << arr[i].toString();
        }
      }
    }
  }

  if (p_obj.contains("tasks")) {
    if (!p_mergeTasks) {
      p_task->m_children.clear();
    }

    auto arr = p_obj["tasks"].toArray();
    for (int i = 0; i < arr.size(); ++i) {
      QScopedPointer<Task> childTask(
          new Task(p_task->m_locale, p_task->getFile(), p_task->m_taskMgr, p_task));
      if (fromJson(childTask.data(), arr[i].toObject())) {
        connect(childTask.data(), &Task::outputRequested, p_task, &Task::outputRequested);
        p_task->m_children.append(childTask.take());
      }
    }
  }

  if (p_obj.contains("inputs")) {
    p_task->m_dto.inputs.clear();
    auto arr = p_obj["inputs"].toArray();
    for (int i = 0; i < arr.size(); ++i) {
      const auto inputObj = arr[i].toObject();
      InputDTO input;
      if (inputObj.contains("id")) {
        input.id = inputObj["id"].toString();
      } else {
        qWarning() << "Input configuration not contains id";
      }

      if (inputObj.contains("type")) {
        input.type = inputObj["type"].toString();
      } else {
        input.type = "promptString";
      }

      if (inputObj.contains("description")) {
        input.description = getLocaleString(inputObj["description"], p_task->m_locale);
      }

      if (inputObj.contains("default")) {
        input.default_ = getLocaleString(inputObj["default"], p_task->m_locale);
      }

      if (input.type == "promptString" && inputObj.contains("password")) {
        input.password = inputObj["password"].toBool();
      } else {
        input.password = false;
      }

      if (input.type == "pickString") {
        if (inputObj.contains("options")) {
          input.options = getLocaleStringList(inputObj["options"], p_task->m_locale);
        }

        if (!input.default_.isNull() && !input.options.contains(input.default_)) {
          qWarning() << "default of input must be one of the option values";
        }
      }

      p_task->m_dto.inputs << input;
    }
  }

  if (p_obj.contains("messages")) {
    p_task->m_dto.messages.clear();
    auto arr = p_obj["messages"].toArray();
    for (int i = 0; i < arr.size(); ++i) {
      const auto msgObj = arr[i].toObject();
      MessageDTO msg;
      if (msgObj.contains("id")) {
        msg.id = msgObj["id"].toString();
      } else {
        qWarning() << "Message configuration not contain id";
      }

      if (msgObj.contains("type")) {
        msg.type = msgObj["type"].toString();
      } else {
        msg.type = "information";
      }

      if (msgObj.contains("title")) {
        msg.title = getLocaleString(msgObj["title"], p_task->m_locale);
      }

      if (msgObj.contains("text")) {
        msg.text = getLocaleString(msgObj["text"], p_task->m_locale);
      }

      if (msgObj.contains("detailedText")) {
        msg.detailedText = getLocaleString(msgObj["detailedText"], p_task->m_locale);
      }

      if (msgObj.contains("buttons")) {
        auto buttonsArr = msgObj["buttons"].toArray();
        for (int j = 0; j < buttonsArr.size(); ++j) {
          const auto btnObj = buttonsArr[j].toObject();
          ButtonDTO btn;
          btn.text = getLocaleString(btnObj["text"], p_task->m_locale);
          msg.buttons << btn;
        }
      }

      p_task->m_dto.messages << msg;
    }
  }

  // OS-specific task configuration
#if defined(Q_OS_WIN)
#define OS_SPEC "windows"
#elif defined(Q_OS_MACOS)
#define OS_SPEC "osx"
#else
#define OS_SPEC "linux"
#endif

  if (p_obj.contains(OS_SPEC)) {
    const auto osObj = p_obj[OS_SPEC].toObject();
    fromJsonV0(p_task, osObj, true);
  }

#undef OS_SPEC

  return true;
}

const QString &Task::getVersion() const { return m_dto.version; }

const QString &Task::getType() const { return m_dto.type; }

QString Task::getCommand() { return variableMgr().evaluate(this, m_dto.command); }

QStringList Task::getArgs() { return variableMgr().evaluate(this, m_dto.args); }

const QString &Task::getLabel() const { return m_dto.label; }

const QString &Task::getIcon() const { return m_dto.icon; }

const QString &Task::getShortcut() const { return m_dto.shortcut; }

QString Task::getOptionsCwd() {
  auto cwd = m_dto.options.cwd;
  if (!cwd.isNull()) {
    return variableMgr().evaluate(this, cwd);
  }

  auto notebook = TaskVariableMgr::getCurrentNotebook();
  if (notebook) {
    cwd = notebook->getRootFolderAbsolutePath();
  }

  if (!cwd.isNull()) {
    return cwd;
  }

  auto buffer = TaskVariableMgr::getCurrentBuffer();
  if (buffer) {
    return QFileInfo(buffer->getPath()).dir().absolutePath();
  }

  return QFileInfo(m_dto._source).dir().absolutePath();
}

const QMap<QString, QString> &Task::getOptionsEnv() const { return m_dto.options.env; }

const QString &Task::getOptionsShellExecutable() const { return m_dto.options.shell.executable; }

QStringList Task::getOptionsShellArgs() {
  if (m_dto.options.shell.args.isEmpty()) {
    return ShellExecution::defaultShellArguments(m_dto.options.shell.executable);
  } else {
    return variableMgr().evaluate(this, m_dto.options.shell.args);
  }
}

const QVector<Task *> &Task::getChildren() const { return m_children; }

const QVector<InputDTO> &Task::getInputs() const { return m_dto.inputs; }

const InputDTO *Task::findInput(const QString &p_id) const {
  for (const auto &input : m_dto.inputs) {
    if (input.id == p_id) {
      return &input;
    }
  }

  qWarning() << "input" << p_id << "not found for task" << getLabel();
  return nullptr;
}

const MessageDTO *Task::findMessage(const QString &p_id) const {
  for (const auto &msg : m_dto.messages) {
    if (msg.id == p_id) {
      return &msg;
    }
  }

  qWarning() << "message" << p_id << "not found for task" << getLabel();
  return nullptr;
}

const QString &Task::getFile() const { return m_dto._source; }

Task::Task(const QString &p_locale, const QString &p_file, TaskMgr *p_taskMgr, QObject *p_parent)
    : QObject(p_parent), m_taskMgr(p_taskMgr), m_locale(p_locale) {
  m_dto._source = p_file;
  m_dto.version = s_latestVersion;
  m_dto.type = "shell";
  m_dto.options.shell.executable = ShellExecution::defaultShell();

  // Inherit configuration.
  m_parent = qobject_cast<Task *>(p_parent);
  if (m_parent) {
    m_dto.version = m_parent->m_dto.version;
    m_dto.type = m_parent->m_dto.type;
    m_dto.command = m_parent->m_dto.command;
    m_dto.args = m_parent->m_dto.args;
    m_dto.options.cwd = m_parent->m_dto.options.cwd;
    m_dto.options.env = m_parent->m_dto.options.env;
    m_dto.options.shell.executable = m_parent->m_dto.options.shell.executable;
    m_dto.options.shell.args = m_parent->m_dto.options.shell.args;
    // Do not inherit label/inputs/tasks.
  } else {
    m_dto.label = QFileInfo(p_file).baseName();
  }
}

QProcess *Task::setupProcess() {
  setCancelled(false);

  auto command = getCommand();
  if (command.isEmpty()) {
    return nullptr;
  }

  QScopedPointer<QProcess> scopedProcess(new QProcess(this));

  auto process = scopedProcess.data();
  process->setWorkingDirectory(getOptionsCwd());

  const auto &optionsEnv = getOptionsEnv();
  if (!optionsEnv.isEmpty()) {
    auto env = QProcessEnvironment::systemEnvironment();
    for (auto it = optionsEnv.begin(); it != optionsEnv.end(); it++) {
      env.insert(it.key(), it.value());
    }
    process->setProcessEnvironment(env);
  }

  const auto args = getArgs();
  const auto &type = getType();

  if (type == "shell") {
    ShellExecution::setupProcess(process, command, args, getOptionsShellExecutable(),
                                 getOptionsShellArgs());
  } else if (getType() == "process") {
    process->setProgram(command);
    process->setArguments(args);
  }

  if (isCancelled()) {
    return nullptr;
  }

  scopedProcess.take();

  connect(process, &QProcess::started, this,
          [this]() { emit outputRequested(tr("[Task (%1) started]\n").arg(getLabel())); });
  connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
    auto text = decodeText(process->readAllStandardOutput());
    // TODO: interaction with process.
    emit outputRequested(text);
  });
  connect(process, &QProcess::readyReadStandardError, this, [this, process]() {
    auto text = process->readAllStandardError();
    emit outputRequested(decodeText(text));
  });
  connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    emit outputRequested(tr("[Task (%1) error occurred (%2)]\n").arg(getLabel()).arg(error));
  });
  connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, process](int exitCode) {
            emit outputRequested(tr("\n[Task (%1) finished (%2)]\n").arg(getLabel()).arg(exitCode));
            process->deleteLater();
          });

  return process;
}

void Task::run() {
  QProcess *process;
  try {
    process = setupProcess();
  } catch (const char *msg) {
    qWarning() << "exception while setup process" << msg;
    return;
  }

  if (process) {
    qDebug() << "run task" << process->program() << process->arguments();
    process->start();
  }
}

const TaskDTO &Task::getDTO() const { return m_dto; }

QString Task::decodeText(const QByteArray &p_text) {
  static QByteArrayList codecNames = {"UTF-8", "System", "UTF-16", "GB18030"};

  for (const auto &name : codecNames) {
    auto text = decodeText(p_text, name);
    if (!text.isNull()) {
      return text;
    }
  }

  return QString::fromLocal8Bit(p_text);
}

QString Task::decodeText(const QByteArray &p_text, const QByteArray &p_name) {
  auto codec = QTextCodec::codecForName(p_name);
  if (codec) {
    QTextCodec::ConverterState state;
    auto text = codec->toUnicode(p_text.data(), p_text.size(), &state);
    if (state.invalidChars > 0) {
      return QString();
    }
    return text;
  }
  return QString();
}

QString Task::getLocaleString(const QJsonValue &p_value, const QString &p_locale) {
  if (p_value.isObject()) {
    auto obj = p_value.toObject();
    if (obj.contains(p_locale)) {
      return obj.value(p_locale).toString();
    } else {
      qWarning() << "value of locale not found" << p_locale;
      if (!obj.isEmpty()) {
        return obj.begin().value().toString();
      } else {
        return QString();
      }
    }
  } else {
    return p_value.toString();
  }
}

QStringList Task::getLocaleStringList(const QJsonValue &p_value, const QString &p_locale) {
  QStringList strs;
  const auto arr = p_value.toArray();
  for (int i = 0; i < arr.size(); ++i) {
    strs << getLocaleString(arr[i], p_locale);
  }
  return strs;
}

QStringList Task::getStringList(const QJsonValue &p_value) {
  QStringList strs;
  const auto arr = p_value.toArray();
  for (int i = 0; i < arr.size(); ++i) {
    strs << arr[i].toString();
  }
  return strs;
}

const TaskVariableMgr &Task::variableMgr() const { return m_taskMgr->getVariableMgr(); }

bool Task::isCancelled() const { return m_cancelled; }

void Task::setCancelled(bool p_cancelled) { m_cancelled = p_cancelled; }
