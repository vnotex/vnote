#include "graphhelper.h"

#include <QDebug>
#include <QFileInfo>

#include <utils/processutils.h>

using namespace vnotex;

#define TaskIdProperty "GraphTaskId"
#define TaskTimeStampProperty "GraphTaskTimeStamp"

GraphHelper::GraphHelper() : m_cache(100, CacheItem()) {}

QStringList GraphHelper::getArgsToUse(const QStringList &p_args) {
  if (p_args.isEmpty()) {
    return QStringList();
  }

  if (p_args[0] == "-c") {
    // Combine all the arguments except the first one.
    QStringList args;
    args << p_args[0];

    QString subCmd;
    for (int i = 1; i < p_args.size(); ++i) {
      subCmd += " " + p_args[i];
    }
    args << subCmd;

    return args;
  } else {
    return p_args;
  }
}

void GraphHelper::process(quint64 p_id, TimeStamp p_timeStamp, const QString &p_format,
                          const QString &p_text, QObject *p_owner, const ResultCallback &p_callback,
                          int p_imageIndex) {
  Task task;
  task.m_id = p_id;
  task.m_timeStamp = p_timeStamp;
  task.m_format = p_format;
  task.m_text = p_text;
  task.m_imageIndex = p_imageIndex;
  task.m_owner = p_owner;
  task.m_callback = p_callback;

  m_tasks.enqueue(task);

  processOneTask();
}

void GraphHelper::processOneTask() {
  if (m_taskOngoing || m_tasks.isEmpty()) {
    return;
  }

  m_taskOngoing = true;

  const auto &task = m_tasks.head();

  const auto &cachedData = m_cache.get(qMakePair(task.m_text, task.m_imageIndex));
  if (!cachedData.isNull() && cachedData.m_format == task.m_format) {
    finishOneTask(cachedData.m_data);
    return;
  }

  if (!m_programValid) {
    qWarning() << "GraphHelper: program to execute for rendering is not valid. program="
               << m_program << "overriddenCommand=" << m_overriddenCommand
               << "task id=" << task.m_id << "format=" << task.m_format;
    const auto failedTask = m_tasks.dequeue();
    callbackOneTask(failedTask, failedTask.m_id, failedTask.m_timeStamp, failedTask.m_format,
                    QString(), false);
    m_taskOngoing = false;
    processOneTask();
    return;
  }

  // Will be released in finishOneTask.
  QProcess *process = new QProcess();
  process->setProperty(TaskIdProperty, task.m_id);
  process->setProperty(TaskTimeStampProperty, task.m_timeStamp);
  QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                   [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                     finishOneTask(process, exitCode, exitStatus);
                   });

  QObject::connect(process, &QProcess::errorOccurred,
                   [this, process](QProcess::ProcessError error) {
                     if (error == QProcess::FailedToStart) {
                       finishOneTask(process, -1, QProcess::CrashExit);
                     }
                   });

  const auto imageArgs = getImageArgs(task.m_imageIndex);
  const auto input = task.m_text.toUtf8();
  const auto taskId = task.m_id;
  if (m_overriddenCommand.isEmpty()) {
    Q_ASSERT(!m_program.isEmpty());
    QStringList args(m_args);
    args << getFormatArgs(task.m_format) << imageArgs;
    const auto argsToUse = getArgsToUse(args);
    qInfo() << "GraphHelper: starting render task id=" << task.m_id
            << "timeStamp=" << task.m_timeStamp << "format=" << task.m_format
            << "textLen=" << task.m_text.size() << "program=" << m_program << "args=" << argsToUse;
    process->start(m_program, argsToUse);
  } else {
    auto cmd = getCommandToUse(m_overriddenCommand, task.m_format);
    if (!imageArgs.isEmpty()) {
      cmd += QLatin1Char(' ') + imageArgs.join(QLatin1Char(' '));
    }
    qInfo() << "GraphHelper: starting render task id=" << task.m_id
            << "timeStamp=" << task.m_timeStamp << "format=" << task.m_format
            << "textLen=" << task.m_text.size() << "overriddenCommand=" << cmd;
    process->start(cmd);
  }

  if (process->write(input) == -1) {
    qWarning() << "Graph task" << taskId
               << "failed to write to process stdin:" << process->errorString();
  }

  process->closeWriteChannel();
}

void GraphHelper::finishOneTask(QProcess *p_process, int p_exitCode,
                                QProcess::ExitStatus p_exitStatus) {
  Q_ASSERT(m_taskOngoing && !m_tasks.isEmpty());

  const auto task = m_tasks.dequeue();

  const quint64 id = p_process->property(TaskIdProperty).toULongLong();
  const quint64 timeStamp = p_process->property(TaskTimeStampProperty).toULongLong();
  Q_ASSERT(task.m_id == id && task.m_timeStamp == timeStamp);

  qDebug() << "Graph task" << id << timeStamp << "finished";

  const bool success = p_exitStatus == QProcess::NormalExit && p_exitCode == 0;
  QString data;
  if (p_exitStatus == QProcess::NormalExit) {
    const auto outBa = p_process->readAllStandardOutput();
    data = task.m_format == QStringLiteral("svg") ? QString::fromUtf8(outBa)
                                                  : QString::fromLatin1(outBa.toBase64());
    if (success) {
      CacheItem item;
      item.m_format = task.m_format;
      item.m_data = data;
      m_cache.set(qMakePair(task.m_text, task.m_imageIndex), item);
    }
  } else {
    qWarning() << "Graph task" << id << "failed to start / crashed. exitCode=" << p_exitCode
               << "exitStatus=" << p_exitStatus << "processError=" << p_process->error()
               << "errorString=" << p_process->errorString();
  }

  const QByteArray errBa = p_process->readAllStandardError();
  if (!errBa.isEmpty()) {
    QString errStr(QString::fromUtf8(errBa));
    if (!success) {
      qWarning() << "Graph task" << id << "stderr:" << errStr;
    } else {
      qDebug() << "Graph task" << id << "stderr:" << errStr;
    }
  }

  callbackOneTask(task, id, task.m_timeStamp, task.m_format, data, success);

  p_process->deleteLater();

  m_taskOngoing = false;
  processOneTask();
}

void GraphHelper::finishOneTask(const QString &p_data) {
  Q_ASSERT(m_taskOngoing && !m_tasks.isEmpty());

  const auto task = m_tasks.dequeue();

  qDebug() << "Graph task" << task.m_id << task.m_timeStamp << "finished by cache" << p_data.size();

  callbackOneTask(task, task.m_id, task.m_timeStamp, task.m_format, p_data, true);

  m_taskOngoing = false;
  processOneTask();
}

QString GraphHelper::getCommandToUse(const QString &p_command, const QString &p_format) {
  auto cmd(p_command);
  cmd.replace("%1", p_format);
  return cmd;
}

QStringList GraphHelper::getImageArgs(int p_imageIndex) const {
  Q_UNUSED(p_imageIndex);
  return {};
}

void GraphHelper::clearCache() { m_cache.clear(); }

void GraphHelper::checkValidProgram() {
  m_programValid = true;
  if (m_overriddenCommand.isEmpty()) {
    if (m_program.isEmpty()) {
      m_programValid = false;
    } else {
      QFileInfo finfo(m_program);
      m_programValid = !finfo.isAbsolute() || finfo.isExecutable();
    }
  }
  qInfo() << "GraphHelper::checkValidProgram program=" << m_program
          << "overriddenCommand=" << m_overriddenCommand << "valid=" << m_programValid;
}

void GraphHelper::callbackOneTask(const Task &p_task, quint64 p_id, TimeStamp p_timeStamp,
                                  const QString &p_format, const QString &p_data,
                                  bool p_success) const {
  if (p_task.m_owner) {
    p_task.m_callback(p_id, p_timeStamp, p_format, p_data, p_success);
  }
}
