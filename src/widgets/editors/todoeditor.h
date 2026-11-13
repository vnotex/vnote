#ifndef VNOTEX_TODOEDITOR_H
#define VNOTEX_TODOEDITOR_H

#include <QPointer>
#include <QWidget>

#include <core/tododocument.h>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QModelIndex;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;

namespace vnotex {
class ServiceLocator;
class SimpleMarkdownEditor;
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
  void focusTasks();
  void focusGroup(const QString &p_key);
  QString selectedGroupKey() const;
  void closeDetails();
  void scroll(bool p_up);
  void zoom(int p_delta);
  int getZoomDelta() const { return m_zoomDelta; }
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
  void refreshDetails();
  void refreshSteps(const TodoItem *p_item);
  void refreshTags(const TodoItem *p_item);
  void refreshQuickAdd();
  void updateLayout();
  void updateSidebarGeometry();
  void setSidebarOpen(bool p_open);
  void selectView(int p_row);
  void openTask(const QModelIndex &p_index);
  void addTask();
  void addList(const QString &p_groupKey = {});
  void addGroup();
  void renameGroup(const QString &p_key);
  void moveGroup(const QString &p_key, int p_direction);
  void showGroupMenu(const QString &p_key, const QPoint &p_globalPosition);
  void toggleGroup(const QString &p_key);
  void moveListToGroup(const QString &p_key, const QString &p_groupKey);
  void duplicateList(const QString &p_key);
  void showListMenu(const QString &p_key, const QPoint &p_globalPosition);
  void showTaskMenu(const QString &p_key, const QPoint &p_globalPosition);
  void renameList(const QString &p_key);
  void chooseListIcon();
  void refreshListIcon();
  void deleteList(const QString &p_key);
  void deleteTask(const QString &p_key);
  void moveTaskToList(const QString &p_key);
  void moveTask(const QString &p_key, int p_direction);
  void moveList(const QString &p_key, int p_direction);
  void moveStep(int p_direction);
  void updateStep(bool p_typing);
  void editTags(const QString &p_add, int p_remove = -1);
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
  QPushButton *m_newList = nullptr;
  QPushButton *m_listIcon = nullptr;
  QLineEdit *m_listTitle = nullptr;
  QLabel *m_viewTitle = nullptr;
  SimpleMarkdownEditor *m_description = nullptr;
  QPushButton *m_listMore = nullptr;
  QComboBox *m_sort = nullptr;
  QPushButton *m_tagFilter = nullptr;
  QListView *m_tasks = nullptr;
  QComboBox *m_destination = nullptr;
  QCheckBox *m_addHasDue = nullptr;
  QDateEdit *m_addDue = nullptr;
  QLineEdit *m_addTask = nullptr;
  QPushButton *m_addButton = nullptr;
  QPushButton *m_back = nullptr;
  QLineEdit *m_title = nullptr;
  QCheckBox *m_checked = nullptr;
  QCheckBox *m_important = nullptr;
  QCheckBox *m_myDay = nullptr;
  QLabel *m_myDayDate = nullptr;
  QCheckBox *m_hasDue = nullptr;
  QDateEdit *m_due = nullptr;
  QPushButton *m_clearDue = nullptr;
  QListWidget *m_steps = nullptr;
  QLineEdit *m_stepTitle = nullptr;
  QCheckBox *m_stepChecked = nullptr;
  QPushButton *m_stepUp = nullptr;
  QPushButton *m_stepDown = nullptr;
  QPushButton *m_stepDelete = nullptr;
  QLineEdit *m_addStep = nullptr;
  QWidget *m_tagChips = nullptr;
  QHBoxLayout *m_tagLayout = nullptr;
  QLineEdit *m_tagInput = nullptr;
  SimpleMarkdownEditor *m_note = nullptr;
  QPushButton *m_move = nullptr;
  QPushButton *m_delete = nullptr;
  QString m_detailsKey;
  QString m_lastDestination;
  QStringList m_renderedTags;
  QPointer<QWidget> m_typingField;
  quint64 m_generation = 0;
  quint64 m_typingSession = 0;
  int m_zoomDelta = 0;
  int m_stepIndex = -1;
  bool m_editable = false;
  bool m_updating = false;
  bool m_committing = false;
  bool m_narrow = false;
  bool m_detailsOpen = false;
  bool m_groupFocused = false;
  bool m_searchComposing = false;
};
} // namespace vnotex

#endif // VNOTEX_TODOEDITOR_H
