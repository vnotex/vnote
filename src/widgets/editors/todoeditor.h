#ifndef VNOTEX_TODOEDITOR_H
#define VNOTEX_TODOEDITOR_H

#include <QPointer>
#include <QVector>
#include <QWidget>

#include <core/sessionconfig.h>
#include <core/tododocument.h>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListView;
class QModelIndex;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace vnotex {
class ServiceLocator;
class SimpleMarkdownEditor;
class TodoTaskView;
class TodoViewWindowController;

class TodoEditor : public QWidget {
  Q_OBJECT
public:
  explicit TodoEditor(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~TodoEditor() override;

  void bindController(TodoViewWindowController *p_controller);
  void setEditable(bool p_editable);
  void focusSearch();
  void focusCurrent();
  void focusTasks(bool p_restoreTaskSelection = false);
  void focusGroup(const QString &p_key);
  QString selectedGroupKey() const;
  void closeDetails();
  void scroll(bool p_up);
  QString selectedText() const;
  void invalidateGestures();
  void resetForBuffer();

signals:
  void undoRequested();
  void redoRequested();

protected:
  bool eventFilter(QObject *p_object, QEvent *p_event) override;
  void resizeEvent(QResizeEvent *p_event) override;

private:
  struct Gesture {
    QPointer<TodoViewWindowController> m_controller;
    quint64 m_generation = 0;
    QString m_source;
    QString m_selection;
    QString m_groupSelection;
  };

  void createUi();
  void createDetails();
  void refresh();
  void applySortOrder();
  void refreshDetails();
  void refreshSteps(const TodoItem *p_item, bool p_reset);
  void showStepMenu(int p_index, const QPoint &p_globalPosition);
  void showDueMenu();
  void pickDueDate();
  void refreshQuickAdd();
  void updateLayout(bool p_resized = false);
  void updateSidebarGeometry();
  void setSidebarOpen(bool p_open);
  void selectView(int p_row);
  void openTask(const QModelIndex &p_index);
  void addTask();
  void addList(const QString &p_groupKey = {});
  void addGroup();
  void archiveCompletedTasks();
  void renameGroup(const QString &p_key);
  void moveGroup(const QString &p_key, int p_direction);
  void showGroupMenu(const QString &p_key, const QPoint &p_globalPosition);
  void toggleGroup(const QString &p_key);
  void moveListToGroup(const QString &p_key, const QString &p_groupKey);
  void duplicateList(const QString &p_key);
  void showListMenu(const QString &p_key, const QPoint &p_globalPosition);
  void showTaskMenu(const QString &p_key, const QPoint &p_globalPosition);
  void renameList(const QString &p_key);
  void editListDescription(const QString &p_key);
  void chooseListIcon();
  void refreshListIcon();
  void refreshHeadingFont();
  void deleteList(const QString &p_key);
  void deleteTask(const QString &p_key);
  void toggleTaskMyDay(const QString &p_key);
  void moveTask(const QString &p_key, int p_direction);
  void moveList(const QString &p_key, int p_direction);
  void updateStep(int p_index, bool p_typing);
  template <typename Update>
  void updateTask(const Update &p_update, QWidget *p_typingField = nullptr);
  template <typename Command>
  bool command(const Command &p_command, QWidget *p_typingField = nullptr);
  void endTyping();
  bool canMutate() const;
  bool preservesField(QWidget *p_field) const;
  const TodoItem *selectedItem() const;
  Gesture gesture() const;
  bool accepts(const Gesture &p_gesture) const;
  bool acceptsNavigation(const Gesture &p_gesture) const;
  void showTextMenu(QWidget *p_widget, const QPoint &p_position);

  ServiceLocator &m_services;
  QPointer<TodoViewWindowController> m_controller;
  SessionConfig::TodoEditorLayout m_paneLayout;
  QSplitter *m_splitter = nullptr;
  QWidget *m_sidebar = nullptr;
  QWidget *m_navigation = nullptr;
  QWidget *m_taskPane = nullptr;
  QWidget *m_details = nullptr;
  QStackedWidget *m_pages = nullptr;
  QScrollArea *m_detailsScroll = nullptr;
  QLineEdit *m_search = nullptr;
  QListView *m_lists = nullptr;
  QPushButton *m_listsButton = nullptr;
  QPushButton *m_newGroup = nullptr;
  QPushButton *m_archiveCompleted = nullptr;
  QPushButton *m_newList = nullptr;
  QPushButton *m_listIcon = nullptr;
  QLineEdit *m_listTitle = nullptr;
  QLabel *m_viewTitle = nullptr;
  QLabel *m_description = nullptr;
  QPushButton *m_listMore = nullptr;
  TodoTaskView *m_tasks = nullptr;
  QComboBox *m_destination = nullptr;
  QCheckBox *m_addHasDue = nullptr;
  QDateEdit *m_addDue = nullptr;
  QLineEdit *m_addTask = nullptr;
  QPushButton *m_addButton = nullptr;
  QToolButton *m_back = nullptr;
  QLineEdit *m_title = nullptr;
  QCheckBox *m_checked = nullptr;
  QToolButton *m_important = nullptr;
  QToolButton *m_myDay = nullptr;
  QPushButton *m_due = nullptr;
  struct StepRow {
    QWidget *m_widget;
    QCheckBox *m_checked;
    QLineEdit *m_title;
    QToolButton *m_more;
  };
  QWidget *m_steps = nullptr;
  QVBoxLayout *m_stepLayout = nullptr;
  QVector<StepRow> m_stepRows;
  QLineEdit *m_addStep = nullptr;
  SimpleMarkdownEditor *m_note = nullptr;
  QLabel *m_createdDate = nullptr;
  QToolButton *m_deleteTask = nullptr;
  QString m_detailsKey;
  QString m_lastDestination;
  QPointer<QWidget> m_typingField;
  quint64 m_generation = 0;
  quint64 m_typingSession = 0;
  bool m_editable = false;
  bool m_updating = false;
  bool m_committing = false;
  bool m_narrow = false;
  bool m_detailsOpen = false;
  bool m_closeDetailsOnTaskClick = false;
  bool m_groupFocused = false;
  bool m_searchComposing = false;
};
} // namespace vnotex

#endif // VNOTEX_TODOEDITOR_H
