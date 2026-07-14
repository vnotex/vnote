#ifndef TASKVARIABLEMGR_H
#define TASKVARIABLEMGR_H

#include <core/noncopyable.h>

#include <functional>

#include <QHash>
#include <QProcessEnvironment>
#include <QScopedPointer>
#include <QSharedPointer>
#include <QString>

namespace vnotex {
class Task;
class TaskService;
class ITaskContext;

class TaskVariable {
public:
  typedef std::function<QString(Task *, const QString &)> Func;

  TaskVariable(const QString &p_name, const Func &p_func);

  QString evaluate(Task *p_task, const QString &p_value) const;

private:
  QString m_name;

  Func m_func;
};

class TaskVariableMgr : private Noncopyable {
public:
  explicit TaskVariableMgr(TaskService *p_taskService);

  void init();

  QString evaluate(Task *p_task, const QString &p_text) const;

  QStringList evaluate(Task *p_task, const QStringList &p_texts) const;

  // Used for UT.
  void overrideVariable(const QString &p_name, const TaskVariable::Func &p_func);

private:
  void initVariables();

  void initNotebookVariables();

  void initBufferVariables();

  void initTaskVariables();

  void initMagicVariables();

  void initEnvironmentVariables();

  void initConfigVariables();

  void initInputVariables();

  void initShellVariables();

  void addVariable(const QString &p_name, const TaskVariable::Func &p_func);

  const TaskVariable *findVariable(const QString &p_name) const;

  // Returns the injected UI context, or nullptr if unavailable.
  ITaskContext *context() const;

  TaskService *m_taskService = nullptr;

  QHash<QString, TaskVariable> m_variables;

  bool m_needUpdateSystemEnvironment = true;

  QProcessEnvironment m_systemEnv;

  // %{name[:value]}%.
  // Captured texts:
  // 1 - The name of the variable (trim needed).
  // 2 - The value option of the variable if available (trim needed).
  static const QString c_variableSymbolRegExp;
};
} // namespace vnotex

#endif // TASKVARIABLEMGR_H
