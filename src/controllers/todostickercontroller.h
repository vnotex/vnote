#ifndef VNOTEX_TODOSTICKERCONTROLLER_H
#define VNOTEX_TODOSTICKERCONTROLLER_H

#include <QDate>
#include <QObject>
#include <QPointer>
#include <QVector>
#include <optional>

#include <core/nodeidentifier.h>
#include <core/tododocument.h>
#include <models/todomodels.h>

namespace vnotex {

class HookManager;
class ServiceLocator;

// A source-bound projection only. BufferService owns every read/edit lifetime.
class TodoStickerController : public QObject {
  Q_OBJECT
public:
  explicit TodoStickerController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~TodoStickerController() override;

  void initialize();
  void refresh();
  TodoTaskModel *getTaskModel() const { return m_taskModel; }
  const TodoDocument &getDocument() const { return m_document; }
  const QString &getSelectedListKey() const { return m_selectedListKey; }
  TodoViewKind getViewKind() const { return m_view; }
  void setView(TodoViewKind p_view, const QString &p_listKey = {});
  void setToday(const QDate &p_today);
  void setDueDate(const QDate &p_dueDate);
  const QDate &getDueDate() const { return m_dueDate; }
  QString getFilePath() const { return m_filePath; }
  const QString &getError() const { return m_error; }
  bool isReady() const { return m_ready; }
  bool isBusy() const { return m_editing || m_refreshing; }
  bool canEditTasks() const;
  int addTask(const QString &p_title, const QString &p_destinationKey = {},
              const QDate &p_due = {});
  void openTask(const QString &p_key);
  void edit();
  bool setFilePath(const QString &p_path);

  // Must be set before initialize(); production uses ConfigMgr2's default root.
  void setDefaultNotebookPathForTest(const QString &p_path);

signals:
  void stateChanged();
  void errorOccurred(const QString &p_message);
  void taskAdditionFinished(int p_token, bool p_created);

private slots:
  void handleBufferChanged(const QString &p_bufferId);

private:
  enum class TaskFlag { Checked, Important };

  void subscribe();
  void scheduleRefresh();
  void cancelEdit();
  void changeBinding(const QString &p_path);
  void publishModel();
  void unavailable(const QString &p_error);
  void reportError(const QString &p_error);
  bool resolveFile(const QString &p_path, NodeIdentifier *p_node, QString *p_canonical,
                   QString *p_error, bool p_allowMissing = false) const;
  bool ensureBackingFile(const QString &p_path, NodeIdentifier *p_node, QString *p_canonical,
                         QString *p_error);
  QString resolveDefault(QString *p_error);
  void retarget(const QString &p_notebookId, const QString &p_oldPath,
                const QString &p_newNotebookId, const QString &p_newPath, bool p_folder);
  void setTaskFlag(const QString &p_key, bool p_value, int p_revision, TaskFlag p_flag);
  int applyTaskEdit(const QString &p_key, bool p_value, std::optional<TodoTaskFields> p_newTask,
                    TaskFlag p_flag = TaskFlag::Checked);

  ServiceLocator &m_services;
  TodoTaskModel *m_taskModel;
  QPointer<HookManager> m_hooks;
  QVector<int> m_hookIds;
  TodoDocument m_document;
  NodeIdentifier m_node;
  QString m_filePath;
  QString m_notebookRoot;
  TodoViewKind m_view = TodoViewKind::List;
  QDate m_today = QDate::currentDate();
  QDate m_dueDate;
  QString m_selectedListKey;
  QString m_pendingListKey;
  QString m_pendingTaskKey;
  QString m_error;
  QString m_defaultNotebookPathForTest;
  QString m_currentNotebook;
  quint64 m_generation = 0;
  int m_revision = 0;
  int m_editToken = 0;
  bool m_initialized = false;
  bool m_ready = false;
  bool m_readOnly = true;
  bool m_completedExpanded = true;
  bool m_editing = false;
  bool m_refreshing = false;
  bool m_refreshScheduled = false;
  bool m_refreshAgain = false;
};

} // namespace vnotex

#endif // VNOTEX_TODOSTICKERCONTROLLER_H
