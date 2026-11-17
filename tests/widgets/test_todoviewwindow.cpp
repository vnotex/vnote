#include <QtTest>

#include "../helpers/test_environment.h"

#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#endif

#include <QAbstractButton>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QInputMethodEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMutex>
#include <QMutexLocker>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSemaphore>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

#include <controllers/newnotecontroller.h>
#include <controllers/todoviewwindowcontroller.h>
#include <controllers/viewareacontroller.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/mainconfig.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/memorybuffersource.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/snippetcoreservice.h>
#include <core/services/syncworkqueuemanager.h>
#include <core/services/templateservice.h>
#include <core/services/workspacecoreservice.h>
#include <core/sessionconfig.h>
#include <core/tododocument.h>
#include <core/widgetconfig.h>
#include <gui/services/themeservice.h>
#include <gui/services/viewwindowfactory.h>
#include <models/todomodels.h>
#include <vtextedit/vtextedit.h>
#include <vxcore/vxcore.h>
#include <widgets/dialogs/newnotedialog2.h>
#include <widgets/editors/simplemarkdowneditor.h>
#include <widgets/editors/todoeditor.h>
#include <widgets/inlinebanner.h>
#include <widgets/plaintextedit.h>
#include <widgets/todoviewwindow.h>
#include <widgets/viewarea2.h>
#include <widgets/viewsplit2.h>

#include <cmath>
#include <functional>
#include <memory>
#include <thread>

using namespace vnotex;

namespace tests {
class TodoDesktopSink : public QObject {
  Q_OBJECT
public:
  QList<QUrl> opened;
public slots:
  void open(const QUrl &p_url) { opened.append(p_url); }
};

namespace {
const QByteArray c_source =
    "<!-- todo: {\"version\":1} -->\n\n"
    "<!-- File-scoped author comment -->\n\n"
    "# Default\n<!-- todo-group: {\"id\":\"default\",\"default\":true} -->\n\n"
    "<!-- Default group comment -->\n\n"
    "## Work\n<!-- todo-list: {\"id\":\"work\"} -->\n\n"
    "> Team commitments\n\n"
    "- [ ] Alpha\n  <!-- todo-item: "
    "{\"id\":\"alpha\",\"tags\":[\"work\"],\"private\":\"opaque-metadata-only\"} -->\n\n"
    "  > Existing **note**\n\n"
    "  - [x] First step\n  - [ ] Second step\n\n"
    "<!-- Work-scoped separator between root task lists -->\n\n"
    "+ [ ] Beta\n  <!-- todo-item: {\"id\":\"beta\",\"important\":true} -->\n\n"
    "- [x] Done\n  <!-- todo-item: {\"id\":\"done\"} -->\n\n"
    "## Personal\n<!-- todo-list: {\"id\":\"personal\"} -->\n\n"
    "- [ ] Gamma\n  <!-- todo-item: {\"id\":\"gamma\",\"tags\":[\"home\"]} -->\n";

QByteArray readBytes(const QString &p_path) {
  QFile file(p_path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeBytes(const QString &p_path, const QByteArray &p_bytes) {
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly) && file.write(p_bytes) == p_bytes.size();
}

bool activate(QWidget *p_widget) {
  const QPointer<QWidget> widget(p_widget);
  if (!QTest::qWaitFor([&] { return widget && widget->isVisible(); }))
    return false;
  auto *host = p_widget->window();
  if (!QTest::qWaitForWindowExposed(host))
    return false;
#ifdef Q_OS_WIN
  if (!SetWindowPos(reinterpret_cast<HWND>(host->winId()), HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
    return false;
#endif
  host->raise();
  host->activateWindow();
  QApplication::setActiveWindow(host);
  p_widget->setFocus(Qt::OtherFocusReason);
  return QTest::qWaitFor([=] {
    return host->isActiveWindow() && QGuiApplication::focusWindow() && p_widget->hasFocus();
  });
}

template <typename T> T *control(QObject *p_parent, const char *p_name) {
  return p_parent->findChild<T *>(QString::fromLatin1(p_name));
}

void movePointer(QWidget *p_widget, const QPoint &p_position) {
  const auto global = p_widget->mapToGlobal(p_position);
  QCursor::setPos(global);
  auto *window = p_widget->window();
  QTest::mouseMove(window->windowHandle(), window->mapFromGlobal(global));
}

bool chooseCombo(QComboBox *p_combo, int p_row) {
  if (!p_combo || p_row < 0 || p_row >= p_combo->count() || !activate(p_combo))
    return false;
  p_combo->showPopup();
  const auto index = p_combo->model()->index(p_row, 0);
  p_combo->view()->scrollTo(index);
  QTest::mouseClick(p_combo->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                    p_combo->view()->visualRect(index).center());
  return p_combo->currentIndex() == p_row;
}

bool typeText(QLineEdit *p_edit, const QString &p_text, bool p_replace = true) {
  if (!p_edit || !activate(p_edit))
    return false;
  if (p_replace)
    p_edit->selectAll();
  QTest::keyClicks(p_edit, p_text);
  return p_edit->text() == p_text && p_edit->hasFocus();
}

void replaceDocument(SimpleMarkdownEditor *p_editor, const QString &p_text) {
  QTextCursor cursor(p_editor->document());
  cursor.beginEditBlock();
  cursor.select(QTextCursor::Document);
  cursor.insertText(p_text);
  cursor.endEditBlock();
  p_editor->getTextEdit()->setTextCursor(cursor);
}

QStringList taskTitles(TodoViewWindow *p_window) {
  QStringList titles;
  auto *model = p_window->getController()->getTaskModel();
  for (int row = 0; row < model->rowCount(); ++row) {
    const auto index = model->index(row);
    if (!index.data(TodoTaskModel::SectionRole).toBool())
      titles.append(index.data(TodoTaskModel::TitleRole).toString());
  }
  return titles;
}

bool openTask(TodoViewWindow *p_window, const QString &p_key) {
  auto *view = control<QListView>(p_window, "todoTasks");
  auto *model = p_window->getController()->getTaskModel();
  const auto index = model->index(model->rowForKey(p_key));
  if (!view || !index.isValid() || !activate(view))
    return false;
  view->setCurrentIndex(index);
  view->scrollTo(index);
  QTest::keyClick(view, Qt::Key_Return);
  return QTest::qWaitFor([=] {
    auto *details = control<QWidget>(p_window, "todoDetails");
    return details && details->isVisible() &&
           p_window->getController()->getSelectedItemKey() == p_key;
  });
}

bool selectedTitleReadable(QListView *p_view, const QModelIndex &p_index) {
  const auto image = p_view->viewport()->grab().toImage();
  const auto row = p_view->visualRect(p_index);
  if (image.isNull() || !row.isValid())
    return false;
  const qreal scale = image.devicePixelRatio();
  const auto luminance = [](const QColor &p_color) {
    const auto linear = [](double p_value) {
      return p_value <= 0.04045 ? p_value / 12.92 : std::pow((p_value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(p_color.redF()) + 0.7152 * linear(p_color.greenF()) +
           0.0722 * linear(p_color.blueF());
  };
  const auto background = luminance(
      image.pixelColor(qRound((row.right() - 2) * scale), qRound(row.center().y() * scale)));
  const QRect title(qRound((row.left() + 40) * scale), qRound((row.top() + 4) * scale),
                    qRound(qMax(1, row.width() - 96) * scale),
                    qRound(qMin(row.height() / 2 - 4, p_view->fontMetrics().height() + 4) * scale));
  const auto pixels = title.intersected(image.rect());
  double contrast = 1;
  for (int y = pixels.top(); y <= pixels.bottom(); ++y) {
    for (int x = pixels.left(); x <= pixels.right(); ++x) {
      const auto value = luminance(image.pixelColor(x, y));
      contrast =
          qMax(contrast, (qMax(value, background) + 0.05) / (qMin(value, background) + 0.05));
    }
  }
  return contrast >= 4.5;
}

class SaveGateBarrier {
public:
  SaveGateBarrier(NotebookIoGate &p_gate, const QString &p_notebookId)
      : m_thread([this, &p_gate, p_notebookId]() {
          NotebookIoGate::ScopedLock lock(p_gate, p_notebookId);
          m_started.release();
          m_release.acquire();
        }) {
    m_started.acquire();
  }
  ~SaveGateBarrier() { release(); }
  void release() {
    if (m_thread.joinable()) {
      m_release.release();
      m_thread.join();
    }
  }

private:
  QSemaphore m_started;
  QSemaphore m_release;
  std::thread m_thread;
};

// Real dialogs run nested event loops. An unanswered modal fails, then closes,
// rather than hanging the native test process indefinitely.
class DialogAnswers {
public:
  explicit DialogAnswers(QList<QMessageBox::StandardButton> p_answers,
                         std::function<void(QMessageBox *)> p_inspect = {})
      : m_answers(std::move(p_answers)), m_inspect(std::move(p_inspect)) {
    m_elapsed.start();
    m_timer.setInterval(10);
    QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this] {
      for (auto *widget : QApplication::topLevelWidgets()) {
        auto *box = qobject_cast<QMessageBox *>(widget);
        if (!box || !box->isVisible() || m_answered.contains(box))
          continue;
        if (m_count < m_answers.size()) {
          if (auto *button = box->button(m_answers[m_count])) {
            m_answered.append(box);
            ++m_count;
            if (m_inspect)
              m_inspect(box);
            button->click();
            return;
          }
        }
      }
      if (m_elapsed.elapsed() < 5000)
        return;
      m_timedOut = true;
      QTest::qFail("Unanswered native confirmation dialog", __FILE__, __LINE__);
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible())
          dialog->reject();
      }
      m_timer.stop();
    });
    m_timer.start();
  }
  bool accepted() {
    m_timer.stop();
    return !m_timedOut && m_count == m_answers.size();
  }

private:
  QList<QMessageBox::StandardButton> m_answers;
  std::function<void(QMessageBox *)> m_inspect;
  QList<QPointer<QMessageBox>> m_answered;
  QElapsedTimer m_elapsed;
  QTimer m_timer;
  int m_count = 0;
  bool m_timedOut = false;
};

bool answerDialog(const char *p_name, const std::function<bool(QDialog *)> &p_fill,
                  const std::function<void()> &p_open) {
  bool answered = false;
  QElapsedTimer elapsed;
  elapsed.start();
  QTimer timer;
  timer.setInterval(10);
  QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (dialog && dialog->objectName() == QLatin1String(p_name)) {
      timer.stop();
      answered = p_fill(dialog);
      if (!answered)
        dialog->reject();
    } else if (elapsed.elapsed() > 5000) {
      if (dialog)
        dialog->reject();
      timer.stop();
    }
  });
  timer.start();
  p_open();
  return answered;
}

bool pickListIcon(QDialog *p_dialog, const QString &p_query, bool p_mouse = false) {
  auto *input = control<QLineEdit>(p_dialog, "todoListIconSearch");
  auto *choices = control<QListView>(p_dialog, "emojiChoices");
  if (!input || !choices || !typeText(input, p_query) || !choices->currentIndex().isValid())
    return false;
  if (p_mouse) {
    QTest::mouseClick(choices->viewport(), Qt::LeftButton, Qt::NoModifier,
                      choices->visualRect(choices->currentIndex()).center());
  } else {
    QTest::keyClick(input, Qt::Key_Return);
  }
  return p_dialog->result() == QDialog::Accepted;
}

bool answerNewList(const QString &p_title, const std::function<void()> &p_open) {
  return answerDialog(
      "todoNewListDialog",
      [&](QDialog *p_dialog) {
        auto *name = control<QLineEdit>(p_dialog, "todoNewListName");
        auto *group = control<QComboBox>(p_dialog, "todoNewListGroup");
        auto *accept = control<QPushButton>(p_dialog, "todoNewListAccept");
        if (!name || !group || !accept || !group->currentData().toString().isEmpty() ||
            group->itemText(0) != QLatin1String("No group") ||
            !group->itemData(0).toString().isEmpty())
          return false;
        name->setText(p_title);
        accept->click();
        return true;
      },
      p_open);
}

bool chooseOpenMenu(const char *p_actionName) {
  if (!QTest::qWaitFor([] { return qobject_cast<QMenu *>(QApplication::activePopupWidget()); }))
    return false;
  auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
  for (auto *action : menu->actions()) {
    if (action->objectName() == QLatin1String(p_actionName) && action->isEnabled()) {
      QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                        menu->actionGeometry(action).center());
      return true;
    }
  }
  menu->close();
  return false;
}

bool chooseButtonMenu(QAbstractButton *p_button, const char *p_actionName) {
  if (!p_button || !p_button->isEnabled() || !activate(p_button))
    return false;
  QTest::mouseClick(p_button, Qt::LeftButton);
  return chooseOpenMenu(p_actionName);
}

QMenu *openTaskMenu(TodoViewWindow *p_window, const QString &p_key) {
  auto *tasks = control<QListView>(p_window, "todoTasks");
  if (!tasks || !activate(tasks))
    return nullptr;
  QPoint point;
  {
    auto *model = p_window->getController()->getTaskModel();
    const auto index = model->index(model->rowForKey(p_key));
    if (!index.isValid())
      return nullptr;
    tasks->scrollTo(index);
    point = tasks->visualRect(index).center();
  }
  QContextMenuEvent event(QContextMenuEvent::Mouse, point, tasks->viewport()->mapToGlobal(point));
  QCoreApplication::sendEvent(tasks->viewport(), &event);
  if (!QTest::qWaitFor([] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        return menu && menu->objectName() == QLatin1String("todoTaskMenu");
      }))
    return nullptr;
  return qobject_cast<QMenu *>(QApplication::activePopupWidget());
}

bool chooseTaskMenu(TodoViewWindow *p_window, const QString &p_key, const char *p_actionName) {
  return openTaskMenu(p_window, p_key) && chooseOpenMenu(p_actionName);
}

bool chooseOpenTaskDestination(const QString &p_key) {
  auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
  auto *move = menu ? control<QAction>(menu, "todoMoveTaskAction") : nullptr;
  if (!move || !move->isEnabled() || !move->menu())
    return false;
  auto *submenu = move->menu();
  menu->setActiveAction(move);
  QTest::keyClick(menu, menu->layoutDirection() == Qt::RightToLeft ? Qt::Key_Left : Qt::Key_Right);
  if (!QTest::qWaitFor([=] { return submenu->isVisible(); }))
    return false;
  for (auto *action : submenu->actions()) {
    if (action->data().toString() == p_key && action->isEnabled()) {
      QTest::mouseClick(submenu, Qt::LeftButton, Qt::NoModifier,
                        submenu->actionGeometry(action).center());
      return true;
    }
  }
  menu->close();
  return false;
}

bool chooseOpenSort(const char *p_actionName) {
  auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
  auto *sort = menu ? control<QAction>(menu, "todoSortByAction") : nullptr;
  if (!sort || !sort->isEnabled() || !sort->menu())
    return false;
  auto *submenu = sort->menu();
  menu->setActiveAction(sort);
  QTest::keyClick(menu, menu->layoutDirection() == Qt::RightToLeft ? Qt::Key_Left : Qt::Key_Right);
  if (!QTest::qWaitFor([=] { return submenu->isVisible(); }))
    return false;
  auto *action = control<QAction>(submenu, p_actionName);
  if (!action || !action->isEnabled()) {
    menu->close();
    return false;
  }
  QTest::mouseClick(submenu, Qt::LeftButton, Qt::NoModifier,
                    submenu->actionGeometry(action).center());
  return true;
}

bool chooseSortMenu(TodoViewWindow *p_window, const char *p_actionName) {
  auto *button = control<QPushButton>(p_window, "todoListMore");
  if (!button || !button->isEnabled() || !activate(button))
    return false;
  QTest::mouseClick(button, Qt::LeftButton);
  return chooseOpenSort(p_actionName);
}

bool showLists(TodoViewWindow *p_window) {
  auto *lists = control<QListView>(p_window, "todoLists");
  auto *button = control<QPushButton>(p_window, "todoListsButton");
  if (!lists || !button)
    return false;
  if (!QTest::qWaitFor([=] { return lists->isVisible() || button->isVisible(); }))
    return false;
  if (!lists->isVisible()) {
    if (!activate(button))
      return false;
    QTest::mouseClick(button, Qt::LeftButton);
  }
  return QTest::qWaitFor([=] { return lists->isVisible(); });
}

bool dropOnSidebar(QListView *p_lists, const QModelIndex &p_from, const QPoint &p_position) {
  std::unique_ptr<QMimeData> mime(p_lists->model()->mimeData({p_from}));
  if (!mime)
    return false;
  QDragEnterEvent enter(p_position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(p_lists->viewport(), &enter);
  if (!enter.isAccepted())
    return false;
  QDragMoveEvent move(p_position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(p_lists->viewport(), &move);
  if (!move.isAccepted())
    return false;
  QDropEvent drop(QPointF(p_position), Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(p_lists->viewport(), &drop);
  return drop.isAccepted();
}

bool openGroupMenu(TodoViewWindow *p_window) {
  if (!showLists(p_window))
    return false;
  auto *lists = control<QListView>(p_window, "todoLists");
  const auto key = p_window->getTodoEditor()->selectedGroupKey();
  const auto index = p_window->getController()->getListModel()->index(
      p_window->getController()->getListModel()->rowForGroup(key));
  if (!index.isValid() || !activate(lists))
    return false;
  lists->scrollTo(index);
  const auto point = lists->visualRect(index).center();
  QContextMenuEvent event(QContextMenuEvent::Mouse, point, lists->viewport()->mapToGlobal(point));
  QCoreApplication::sendEvent(lists->viewport(), &event);
  return QTest::qWaitFor([] { return qobject_cast<QMenu *>(QApplication::activePopupWidget()); });
}

bool chooseGroupMenu(TodoViewWindow *p_window, const char *p_actionName) {
  return openGroupMenu(p_window) && chooseOpenMenu(p_actionName);
}

bool toggleGroupInSidebar(TodoViewWindow *p_window) {
  auto *editor = p_window->getTodoEditor();
  editor->focusGroup(editor->selectedGroupKey());
  auto *lists = control<QListView>(p_window, "todoLists");
  if (!lists || !activate(lists))
    return false;
  QTest::keyClick(lists, Qt::Key_Space);
  return true;
}

bool chooseOpenGroupDestination(const QString &p_key) {
  auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
  if (!menu)
    return false;
  auto *move = control<QAction>(menu, "todoMoveListGroupAction");
  if (!move || !move->isEnabled() || !move->menu())
    return false;
  auto *submenu = move->menu();
  menu->setActiveAction(move);
  QTest::keyClick(menu, Qt::Key_Right);
  if (!QTest::qWaitFor([=] { return submenu->isVisible(); }))
    return false;
  for (auto *action : submenu->actions()) {
    if (action->data().toString() == p_key && action->isEnabled()) {
      QTest::mouseClick(submenu, Qt::LeftButton, Qt::NoModifier,
                        submenu->actionGeometry(action).center());
      return true;
    }
  }
  return false;
}

bool chooseMenu(QWidget *p_widget, const QPoint &p_point, const QString &p_label) {
  bool chosen = false;
  QElapsedTimer elapsed;
  elapsed.start();
  QTimer timer;
  timer.setInterval(10);
  QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (menu) {
      for (auto *action : menu->actions()) {
        if (action->text().remove(QLatin1Char('&')) == p_label && action->isEnabled()) {
          chosen = true;
          timer.stop();
          QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                            menu->actionGeometry(action).center());
          return;
        }
      }
      if (elapsed.elapsed() > 5000)
        menu->close();
    }
    if (elapsed.elapsed() > 5000)
      timer.stop();
  });
  timer.start();
  QContextMenuEvent event(QContextMenuEvent::Mouse, p_point, p_widget->mapToGlobal(p_point));
  QCoreApplication::sendEvent(p_widget, &event);
  // TodoEditor's menus use popup(), whereas standard text menus may exec().
  return QTest::qWaitFor([&] { return chosen || !timer.isActive(); }, 6000) && chosen;
}

struct Store {
  QMutex mutex;
  QByteArray bytes = c_source;
  VxCoreError saveError = VXCORE_OK;
  VxCoreError loadError = VXCORE_OK;
  int saves = 0;

  QByteArray snapshot() {
    QMutexLocker lock(&mutex);
    return bytes;
  }
};

MemoryBufferSource memorySource(const QString &p_name, const QByteArray &p_source = c_source) {
  MemoryBufferSource source;
  source.address =
      QStringLiteral("vx://memory/todo-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  source.displayName = p_name;
  source.editorType = QStringLiteral("todo");
  source.content = p_source;
  return source;
}

MemoryBufferSource storedSource(const std::shared_ptr<Store> &p_store) {
  auto source = memorySource(QStringLiteral("Callback planner"));
  source.load = [p_store](QByteArray &p_bytes) {
    QMutexLocker lock(&p_store->mutex);
    p_bytes = p_store->bytes;
    return p_store->loadError;
  };
  source.save = [p_store](QByteArrayView p_bytes) {
    QMutexLocker lock(&p_store->mutex);
    ++p_store->saves;
    if (p_store->saveError == VXCORE_OK)
      p_store->bytes = QByteArray(p_bytes.data(), p_bytes.size());
    return p_store->saveError;
  };
  return source;
}
} // namespace

class TestTodoViewWindow : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();
  void factoryAndDefaultMode_data();
  void factoryAndDefaultMode();
  void externalDispatchPrecedence_data();
  void externalDispatchPrecedence();
  void newNoteDialogCreatesTodo();
  void emptyAndAllTasksQuickAdd();
  void smartQuickAdd_data();
  void smartQuickAdd();
  void nativeTypingSharesSourceUndo();
  void completionCheckmarkContrast();
  void immediateNotesDatesAndSteps();
  void inlineStepTypingAndActions();
  void dueDateMenuAndPicker();
  void detailsGesturesRejectStaleOwner();
  void filteringSortingAndDayRollover();
  void completedToggleKeepsNativeSelection_data();
  void completedToggleKeepsNativeSelection();
  void nativeMoveReorderAndDeletion();
  void taskMenuTogglesAndDueDates();
  void taskMenuMoveOwnershipAndAdmission();
  void taskMenuGesturesRejectStaleOwner();
  void taskMenuAndListShortcuts();
  void taskShortcutsStayScoped();
  void nativeListActions();
  void descriptionDialogRejectsStaleOwner_data();
  void descriptionDialogRejectsStaleOwner();
  void listIconsPersistAndUndo();
  void listIconPickerRejectsStaleOwner_data();
  void listIconPickerRejectsStaleOwner();
  void sidebarDropEvents_data();
  void sidebarDropEvents();
  void sidebarMenusAndDuplication_data();
  void sidebarMenusAndDuplication();
  void nativeGroupsWideAndNarrow_data();
  void nativeGroupsWideAndNarrow();
  void rootListsWideAndNarrow_data();
  void rootListsWideAndNarrow();
  void keyboardGroupsSkipDefault_data();
  void keyboardGroupsSkipDefault();
  void implicitDefaultAndGroupRecreation();
  void groupDialogsRejectForeignReload();
  void deletionConfirmationRejectsForeignReload();
  void invalidSourceRecovery_data();
  void invalidSourceRecovery();
  void repeatedSaveAutosaveAndReopen_data();
  void repeatedSaveAutosaveAndReopen();
  void closeDecisions_data();
  void closeDecisions();
  void saveAndReloadFailuresRetainDraft();
  void dirtyReplacement_data();
  void dirtyReplacement();
  void readOnlyNavigation_data();
  void readOnlyNavigation();
  void sharedBufferRejectsStaleGestures();
  void foreignReloadAndSameTypeReplacement();
  void nativeLayoutsKeyboardAndTheme();
  void groupSourceNavigation_data();
  void groupSourceNavigation();
  void tasksOnlyNavigation_data();
  void tasksOnlyNavigation();
  void tasksOnlyFindAndReplace();
  void unavailableSourceIsNotEmpty();
  void plannedDueDoesNotLeak();
  void storedMyDayRemainsVisible();
  void ownPendingSaveKeepsNewerEdits();
  void manualSaveJoinsPriorAutosave_data();
  void manualSaveJoinsPriorAutosave();
  void activeStaleSnapshotCannotAuthorizeHistory();
  void foreignPendingSplitWaitsForPublication();
  void foreignMemoryPublicationRetiresHistory_data();
  void foreignMemoryPublicationRetiresHistory();
  void ownMemoryCompletionKeepsNewerEdits();

private:
  Buffer2 openNote(const QString &p_name, const QByteArray &p_source = c_source,
                   const FileOpenSettings &p_settings = {});
  TodoViewWindow *current() const;
  void createArea();
  void focusTasks(TodoViewWindow *p_window);
  bool selectView(TodoViewWindow *p_window, TodoViewKind p_kind, const QString &p_listKey = {});
  bool quickAdd(TodoViewWindow *p_window, const QString &p_title);
  QString absolute(const QString &p_name) const;

  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<QTemporaryDir> m_directory;
  QList<QPair<QByteArray, QByteArray>> m_environment;
  QStringList m_searchPaths;
  QStringList m_libraryPaths;
  QPalette m_palette;
  QString m_styleSheet;
  Qt::ColorScheme m_colorScheme = Qt::ColorScheme::Unknown;
  std::unique_ptr<QMimeData> m_clipboard;
  std::unique_ptr<ServiceLocator> m_services;
  std::unique_ptr<ConfigCoreService> m_configCore;
  std::unique_ptr<ConfigMgr2> m_config;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookIoGate> m_gate;
  std::unique_ptr<SyncWorkQueueManager> m_queues;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<BufferService> m_buffers;
  std::unique_ptr<WorkspaceCoreService> m_workspaces;
  std::unique_ptr<FileTypeCoreService> m_fileTypes;
  std::unique_ptr<SnippetCoreService> m_snippets;
  std::unique_ptr<TemplateService> m_templates;
  std::unique_ptr<ThemeService> m_theme;
  std::unique_ptr<ViewWindowFactory> m_factory;
  std::unique_ptr<ViewArea2> m_area;
  QString m_notebook;
};

void TestTodoViewWindow::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  vxcore_set_test_mode(1);
#ifdef Q_OS_WIN
  QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
#endif
}

void TestTodoViewWindow::init() {
  m_directory = std::make_unique<QTemporaryDir>();
  QVERIFY(m_directory->isValid());
  for (const char *name : {"TMP", "TEMP", "TMPDIR"}) {
    m_environment.append({QByteArray(name), qgetenv(name)});
    QVERIFY(qputenv(name, QFile::encodeName(m_directory->path())));
  }
  m_searchPaths = QDir::searchPaths(QStringLiteral("app"));
  m_libraryPaths = QCoreApplication::libraryPaths();
  m_palette = qApp->palette();
  m_styleSheet = qApp->styleSheet();
  m_colorScheme = QGuiApplication::styleHints()->colorScheme();
  m_clipboard = std::make_unique<QMimeData>();
  if (const auto *mime = QApplication::clipboard()->mimeData()) {
    for (const auto &format : mime->formats())
      m_clipboard->setData(format, mime->data(format));
  }
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  m_configCore = std::make_unique<ConfigCoreService>(m_context);
  m_config = std::make_unique<ConfigMgr2>(m_configCore.get());
  m_config->init();
  TestEnvironment{}.apply(*m_config);
  const QString rcc = QStringLiteral(VNOTE_TEST_EXTRA_RCC);
  QVERIFY2(QFileInfo::exists(rcc), qPrintable(rcc));
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  m_config->initAfterQtAppStarted();
  QVERIFY(m_config->extraDataCopyFailures().isEmpty());
  m_hooks = std::make_unique<HookManager>();
  m_gate = std::make_unique<NotebookIoGate>();
  m_queues = std::make_unique<SyncWorkQueueManager>();
  m_notebooks = std::make_unique<NotebookCoreService>(m_context);
  m_notebooks->setHookManager(m_hooks.get());
  m_notebooks->setNotebookIoGate(m_gate.get());
  m_buffers =
      std::make_unique<BufferService>(m_context, m_hooks.get(), m_gate.get(), AutoSavePolicy::None);
  m_buffers->setSyncWorkQueueManager(m_queues.get());
  m_workspaces = std::make_unique<WorkspaceCoreService>(m_context);
  m_workspaces->setHookManager(m_hooks.get());
  m_fileTypes = std::make_unique<FileTypeCoreService>(m_context, QStringLiteral("en_US"));
  m_snippets = std::make_unique<SnippetCoreService>(m_context);
  m_templates = std::make_unique<TemplateService>(m_config.get());
  ThemeServiceConfig theme;
  theme.themeName = QStringLiteral("pure");
  theme.locale = QStringLiteral("en_US");
  theme.appDataPath = QStringLiteral(VNOTE_SRC_DIR "/data/extra");
  m_theme = std::make_unique<ThemeService>(theme);
  m_factory = std::make_unique<ViewWindowFactory>();
  m_factory->registerBuiltInCreators();
  m_services = std::make_unique<ServiceLocator>();
  m_services->registerService(m_configCore.get());
  m_services->registerService(m_config.get());
  m_services->registerService(m_hooks.get());
  m_services->registerService(m_gate.get());
  m_services->registerService(m_queues.get());
  m_services->registerService(m_notebooks.get());
  m_services->registerService(m_buffers.get());
  m_services->registerService(m_workspaces.get());
  m_services->registerService(m_fileTypes.get());
  m_services->registerService(m_snippets.get());
  m_services->registerService(m_templates.get());
  m_services->registerService(m_theme.get());
  m_services->registerService(m_factory.get());
  m_notebook = m_notebooks->createNotebook(m_directory->filePath(QStringLiteral("notebook")),
                                           QStringLiteral("{\"name\":\"TODO native tests\"}"),
                                           NotebookType::Bundled);
  QVERIFY(!m_notebook.isEmpty());
}

void TestTodoViewWindow::cleanup() {
  if (m_buffers)
    m_buffers->setAutoSavePolicy(AutoSavePolicy::None);
  if (m_area) {
    m_area->getController()->setShouldPropagateToCore(false);
    m_area->getController()->closeAll(m_area->getVisibleWorkspaceIds().toVector(), true);
  }
  const bool drained = !m_buffers || m_buffers->shutdown(10000);
  // Notebook close dispatches hooks installed by the still-live area controller.
  if (m_notebooks && !m_notebook.isEmpty())
    m_notebooks->closeNotebook(m_notebook);
  m_area.reset();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  m_services.reset();
  m_factory.reset();
  m_templates.reset();
  m_theme.reset();
  m_snippets.reset();
  m_fileTypes.reset();
  m_workspaces.reset();
  m_buffers.reset();
  m_notebooks.reset();
  m_queues.reset();
  m_gate.reset();
  m_hooks.reset();
  m_config.reset();
  m_configCore.reset();
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  m_notebook.clear();
  QDir::setSearchPaths(QStringLiteral("app"), m_searchPaths);
  QCoreApplication::setLibraryPaths(m_libraryPaths);
  qApp->setStyleSheet(m_styleSheet);
  qApp->setPalette(m_palette);
  QGuiApplication::styleHints()->setColorScheme(m_colorScheme);
  if (m_clipboard)
    QApplication::clipboard()->setMimeData(m_clipboard.release());
  for (const auto &entry : m_environment) {
    if (entry.second.isNull())
      qunsetenv(entry.first.constData());
    else
      qputenv(entry.first.constData(), entry.second);
  }
  m_environment.clear();
  m_directory.reset();
  QVERIFY2(drained, "TODO fixture failed to drain its BufferService save queue");
}

void TestTodoViewWindow::createArea() {
  if (m_area)
    return;
  m_area = std::make_unique<ViewArea2>(*m_services);
  m_area->resize(1000, 760);
  m_area->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_area.get()));
  m_area->activateWindow();
  QApplication::setActiveWindow(m_area.get());
}

QString TestTodoViewWindow::absolute(const QString &p_name) const {
  return m_notebooks->buildAbsolutePath(m_notebook, p_name);
}

Buffer2 TestTodoViewWindow::openNote(const QString &p_name, const QByteArray &p_source,
                                     const FileOpenSettings &p_settings) {
  if (m_notebooks->createFile(m_notebook, QString(), p_name).isEmpty() ||
      !writeBytes(absolute(p_name), p_source))
    return {};
  return m_buffers->openBuffer({m_notebook, p_name}, p_settings);
}

TodoViewWindow *TestTodoViewWindow::current() const {
  return m_area ? qobject_cast<TodoViewWindow *>(m_area->getCurrentViewWindow()) : nullptr;
}

void TestTodoViewWindow::focusTasks(TodoViewWindow *p_window) {
  QVERIFY(p_window);
  auto *tasks = control<QListView>(p_window, "todoTasks");
  QVERIFY(tasks);
  QVERIFY(activate(tasks));
  QTRY_VERIFY(tasks->hasFocus());
}

bool TestTodoViewWindow::selectView(TodoViewWindow *p_window, TodoViewKind p_kind,
                                    const QString &p_listKey) {
  auto *controller = p_window->getController();
  const int row = controller->getListModel()->rowForView(p_kind, p_listKey);
  auto *lists = control<QListView>(p_window, "todoLists");
  if (row < 0 || !showLists(p_window))
    return false;
  if (lists->isRowHidden(row)) {
    const auto *group = controller->getListModel()->groupAt(row);
    if (!group)
      return false;
    p_window->getTodoEditor()->focusGroup(group->m_key);
    QTest::keyClick(lists, Qt::Key_Right);
  }
  if (!activate(lists))
    return false;
  const auto index = lists->model()->index(row, 0);
  lists->scrollTo(index);
  QTest::mouseClick(lists->viewport(), Qt::LeftButton, Qt::NoModifier,
                    lists->visualRect(index).center());
  return controller->getProjection().m_view == p_kind &&
         (p_kind != TodoViewKind::List || controller->getProjection().m_listKey == p_listKey);
}

bool TestTodoViewWindow::quickAdd(TodoViewWindow *p_window, const QString &p_title) {
  auto *add = control<QLineEdit>(p_window, "todoAddTask");
  if (!typeText(add, p_title))
    return false;
  QTest::keyClick(add, Qt::Key_Return);
  return add->text().isEmpty() && add->hasFocus();
}

void TestTodoViewWindow::factoryAndDefaultMode_data() {
  QTest::addColumn<bool>("memory");
  QTest::addColumn<bool>("forcedRead");
  QTest::newRow("file-default-edit") << false << false;
  QTest::newRow("file-forced-read") << false << true;
  QTest::newRow("memory-lowercase-todo-edit") << true << false;
  QTest::newRow("memory-forced-read") << true << true;
}

void TestTodoViewWindow::factoryAndDefaultMode() {
  QFETCH(bool, memory);
  QFETCH(bool, forcedRead);
  createArea();
  QVERIFY(m_factory->hasCreator(QStringLiteral("TODO")));
  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Read;
  settings.m_forceMode = forcedRead;
  const auto buffer =
      memory ? m_buffers->openMemoryBuffer(memorySource(QStringLiteral("No path.md")), settings)
             : openNote(QStringLiteral("planner.ToDo"), c_source, settings);
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window);
  QCOMPARE(window->getMode(), forcedRead ? ViewWindowMode::Read : ViewWindowMode::Edit);
  QCOMPARE(window->getLatestContent().toUtf8(), c_source);
  QTRY_VERIFY(window->getTodoEditor()->isVisible());
  QVERIFY(!window->isModified());
  QCOMPARE(window->getController()->getDocument().m_lists.size(), 2);
  QCOMPARE(window->getController()->getTaskModel()->getActiveCount(), 2);
  QCOMPARE(buffer.isMemoryBacked(), memory);
  if (memory) {
    QVERIFY(buffer.resolvedPath().isEmpty());
    QVERIFY(!buffer.canSave() && !buffer.canReload());
    QCOMPARE(buffer.editorType(), QStringLiteral("todo"));
  }
  // The actual factory must also pass its mode, not silently use the constructor default.
  std::unique_ptr<ViewWindow2> direct(m_factory->create(QStringLiteral("TODO"), *m_services, buffer,
                                                        nullptr, ViewWindowMode::Read));
  QVERIFY(qobject_cast<TodoViewWindow *>(direct.get()));
  QCOMPARE(direct->getMode(), ViewWindowMode::Read);
  QVERIFY(direct->aboutToClose(true));
}

void TestTodoViewWindow::externalDispatchPrecedence_data() {
  QTest::addColumn<bool>("forced");
  QTest::addColumn<bool>("internal");
  QTest::newRow("external-before-default-edit") << false << false;
  QTest::newRow("forced-read-does-not-override-external") << true << false;
  QTest::newRow("explicit-internal-forced-read") << true << true;
}

void TestTodoViewWindow::externalDispatchPrecedence() {
  QFETCH(bool, forced);
  QFETCH(bool, internal);
  createArea();
  SessionConfig::ExternalProgram program;
  program.m_name = SessionConfig::ExternalProgram::c_systemProgramName;
  program.m_suffixes = {QStringLiteral("todo")};
  m_config->getSessionConfig().setExternalPrograms({program});
  TodoDesktopSink desktop;
  QDesktopServices::setUrlHandler(QStringLiteral("file"), &desktop, "open");
  const auto restore =
      qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("file")); });
  FileOpenSettings settings;
  settings.m_forceMode = forced;
  settings.m_useInternalViewer = internal;
  settings.m_mode = ViewWindowMode::Read;
  const auto buffer = openNote(QStringLiteral("external.todo"), c_source, settings);
  if (internal) {
    QVERIFY(buffer.isValid());
    QVERIFY(current());
    QCOMPARE(current()->getMode(), ViewWindowMode::Read);
    QVERIFY(desktop.opened.isEmpty());
  } else {
    QCOMPARE(desktop.opened,
             QList<QUrl>{QUrl::fromLocalFile(absolute(QStringLiteral("external.todo")))});
    QVERIFY(!current());
    QVERIFY(m_area->findChildren<TodoViewWindow *>().isEmpty());
  }
}

void TestTodoViewWindow::newNoteDialogCreatesTodo() {
  NewNoteDialog2 dialog(*m_services, {m_notebook, QString()});
  auto *types = control<QComboBox>(&dialog, "newNoteFileTypeCombo");
  auto *name = control<QLineEdit>(&dialog, "newNoteNameEdit");
  auto *encrypted = control<QCheckBox>(&dialog, "newNoteEncryptCheckBox");
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  QVERIFY(types && name && encrypted && buttons);
  QVERIFY(dialog.findChild<NewNoteController *>());
  const int todo = types->findData(QStringLiteral("TODO"));
  QVERIFY(todo >= 0);
  QCOMPARE(types->itemText(todo), QStringLiteral("TODO"));
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  types->setCurrentIndex(todo);
  QVERIFY(name->text().endsWith(QLatin1String(".todo")));
  QCOMPARE(m_fileTypes->getFileTypeByName(QStringLiteral("TODO")).preferredSuffix(),
           QStringLiteral("todo"));
  QVERIFY(!encrypted->isEnabled());
  QVERIFY(typeText(name, QStringLiteral("Created planner.todo")));
  QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
  QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
  const auto node = dialog.getNewNodeId();
  QCOMPARE(node.notebookId, m_notebook);
  QCOMPARE(node.relativePath, QStringLiteral("Created planner.todo"));
  QVERIFY(QFileInfo::exists(absolute(node.relativePath)));
  QCOMPARE(readBytes(absolute(node.relativePath)), QByteArray());
  QCOMPARE(m_config->getWidgetConfig().getNewNoteDefaultFileTypeName(), QStringLiteral("TODO"));
  createArea();
  QVERIFY(m_buffers->openBuffer(node).isValid());
  QVERIFY(current());
  QVERIFY(current()->getController()->getDocument().isValid());
  QVERIFY(!current()->isModified());
  NewNoteDialog2 remembered(*m_services, {m_notebook, QString()});
  QCOMPARE(control<QComboBox>(&remembered, "newNoteFileTypeCombo")->currentData().toString(),
           QStringLiteral("TODO"));
}

void TestTodoViewWindow::emptyAndAllTasksQuickAdd() {
  createArea();
  const auto buffer = openNote(QStringLiteral("empty.todo"), QByteArray());
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window);
  focusTasks(window);
  QVERIFY(window->getController()->getDocument().m_lists.isEmpty());
  QVERIFY(!window->isModified());
  QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
  QCOMPARE(window->getLatestContent(), QStringLiteral(""));
  auto *add = control<QLineEdit>(window, "todoAddTask");
  QVERIFY(add);
  QVERIFY(typeText(add, QStringLiteral("   ")));
  QTest::keyClick(add, Qt::Key_Return);
  QVERIFY(!window->isModified());
  QVERIFY(quickAdd(window, QStringLiteral("First task")));
  auto *controller = window->getController();
  QCOMPARE(controller->getDocument().m_lists.size(), 1);
  QCOMPARE(controller->getDocument().m_groups.size(), 1);
  QVERIFY(controller->getDocument().m_groups.first().m_isDefault);
  QVERIFY(window->getLatestContent().contains(QStringLiteral("# Default\n")));
  QVERIFY(window->getLatestContent().contains(QStringLiteral("## Tasks\n")));
  QVERIFY(window->getLatestContent().contains(QStringLiteral("todo-group:")));
  QCOMPARE(controller->getDocument().m_lists.first().m_title, QStringLiteral("Tasks"));
  QCOMPARE(controller->getDocument().m_lists.first().m_items.first().m_fields.m_title,
           QStringLiteral("First task"));
  window->undo();
  QCOMPARE(window->getLatestContent(), QStringLiteral(""));
  window->redo();
  auto *newList = control<QPushButton>(window, "todoNewList");
  QVERIFY(newList);
  QVERIFY(answerNewList(QStringLiteral("Other"), [&] { newList->click(); }));
  QCOMPARE(controller->getDocument().m_lists.size(), 2);
  const auto destinationKey = controller->getProjection().m_listKey;
  QVERIFY(selectView(window, TodoViewKind::All));
  auto *destination = control<QComboBox>(window, "todoDestination");
  QVERIFY(destination && destination->isVisible());
  QVERIFY(chooseCombo(destination, destination->findData(destinationKey)));
  QVERIFY(quickAdd(window, QStringLiteral("From All Tasks")));
  QCOMPARE(controller->getDocument().findList(destinationKey)->m_items.last().m_fields.m_title,
           QStringLiteral("From All Tasks"));
  QVERIFY(taskTitles(window).contains(QStringLiteral("First task")));
  QVERIFY(taskTitles(window).contains(QStringLiteral("From All Tasks")));
}

void TestTodoViewWindow::smartQuickAdd_data() {
  QTest::addColumn<int>("kind");
  QTest::addColumn<bool>("filtered");
  for (const auto kind : {TodoViewKind::MyDay, TodoViewKind::Important, TodoViewKind::Planned}) {
    const QByteArray label = QByteArray::number(int(kind));
    QTest::newRow((label + "-ordinary").constData()) << int(kind) << false;
    QTest::newRow((label + "-search").constData()) << int(kind) << true;
  }
}

void TestTodoViewWindow::smartQuickAdd() {
  QFETCH(int, kind);
  QFETCH(bool, filtered);
  createArea();
  QVERIFY(openNote(QStringLiteral("smart.todo")).isValid());
  auto *window = current();
  QVERIFY(window);
  QVERIFY(selectView(window, static_cast<TodoViewKind>(kind)));
  auto *controller = window->getController();
  const auto today = controller->getProjection().m_today;
  auto *destination = control<QComboBox>(window, "todoDestination");
  QVERIFY(destination && destination->isVisible());
  QVERIFY(chooseCombo(destination, destination->findData(QStringLiteral("id:personal"))));
  if (filtered) {
    QVERIFY(typeText(control<QLineEdit>(window, "todoSearch"), QStringLiteral("no-match")));
  } else if (kind == int(TodoViewKind::Planned)) {
    auto *add = control<QLineEdit>(window, "todoAddTask");
    auto *button = control<QPushButton>(window, "todoAddButton");
    auto *enabled = control<QCheckBox>(window, "todoAddHasDue");
    auto *due = control<QDateEdit>(window, "todoAddDue");
    QVERIFY(add && button && enabled && due);
    QVERIFY(typeText(add, QStringLiteral("Must choose a date")));
    const auto before = window->getLatestContent();
    QTest::keyClick(add, Qt::Key_Return);
    QCOMPARE(window->getLatestContent(), before);
    QVERIFY(!button->isEnabled());
    due->setDate(today.addDays(7));
    enabled->setChecked(true);
  }
  QVERIFY(quickAdd(window, QStringLiteral("Smart addition")));
  const auto *list = controller->getDocument().findList(QStringLiteral("id:personal"));
  QVERIFY(list && list->m_items.size() == 2);
  const auto fields = list->m_items.last().m_fields;
  QCOMPARE(fields.m_title, QStringLiteral("Smart addition"));
  QCOMPARE(fields.m_important, !filtered && kind == int(TodoViewKind::Important));
  QCOMPARE(fields.m_myDay, !filtered && kind == int(TodoViewKind::MyDay) ? today : QDate());
  QCOMPARE(fields.m_due,
           !filtered && kind == int(TodoViewKind::Planned) ? today.addDays(7) : QDate());
  QVERIFY(fields.m_tags.isEmpty());
  if (filtered)
    QVERIFY(taskTitles(window).isEmpty());
  window->undo();
  QCOMPARE(controller->getDocument().findList(QStringLiteral("id:personal"))->m_items.size(), 1);
}

void TestTodoViewWindow::nativeTypingSharesSourceUndo() {
  createArea();
  const QByteArray source = "# Default\n\n## Tasks\n\n- [ ] Original\n";
  QVERIFY(openNote(QStringLiteral("history.todo"), source).isValid());
  auto *window = current();
  QVERIFY(window);
  auto *controller = window->getController();
  const auto oldKey = controller->getDocument().m_lists.first().m_items.first().m_key;
  QVERIFY(oldKey.startsWith(QLatin1String("source:")));
  QVERIFY(openTask(window, oldKey));
  auto *title = control<QLineEdit>(window, "todoTitle");
  QVERIFY(typeText(title, QStringLiteral("Typed title")));
  QCOMPARE(title->cursorPosition(), int(QStringLiteral("Typed title").size()));
  const auto key = controller->getSelectedItemKey();
  QVERIFY(key.startsWith(QLatin1String("id:")));
  QCOMPARE(controller->getDocument().findItem(key)->m_fields.m_title,
           QStringLiteral("Typed title"));
  const auto typed = window->getLatestContent();
  QVERIFY(typed.contains(QLatin1String("todo-item:")));
  QTest::keyClick(title, Qt::Key_Z, Qt::ControlModifier);
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  QCOMPARE(title->text(), QStringLiteral("Original"));
  QVERIFY(title->hasFocus());
  QVERIFY(!window->getSourceEditor()->getTextEdit()->hasFocus());
  QTest::keyClick(title, Qt::Key_Y, Qt::ControlModifier);
  QCOMPARE(window->getLatestContent(), typed);
  QCOMPARE(title->text(), QStringLiteral("Typed title"));
  // Focus changes delimit typing even when the owner and field are unchanged.
  auto *search = control<QLineEdit>(window, "todoSearch");
  QVERIFY(search && activate(search));
  QVERIFY(typeText(title, QStringLiteral("Separate typing group")));
  window->undo();
  QCOMPARE(window->getLatestContent(), typed);
  QCOMPARE(title->text(), QStringLiteral("Typed title"));
  // Existing titles may be transiently empty without deleting the task.
  QVERIFY(activate(search));
  QVERIFY(activate(title));
  title->selectAll();
  QTest::keyClick(title, Qt::Key_Backspace);
  QVERIFY(controller->getDocument().isValid());
  QVERIFY(controller->getDocument().findItem(key)->m_fields.m_title.isEmpty());
  window->undo();
  QCOMPARE(window->getLatestContent(), typed);
  auto *important = control<QToolButton>(window, "todoImportant");
  QVERIFY(important && activate(important));
  QTest::keyClick(important, Qt::Key_Space);
  QVERIFY(controller->getDocument().findItem(key)->m_fields.m_important);
  const auto starred = window->getLatestContent();
  QTest::keyClick(important, Qt::Key_Z, Qt::ControlModifier);
  QCOMPARE(window->getLatestContent(), typed);
  QTest::keyClick(important, Qt::Key_Y, Qt::ControlModifier);
  QCOMPARE(window->getLatestContent(), starred);
  QCOMPARE(controller->getSelectedItemKey(), key);
  QVERIFY(openTask(window, key));
  QVERIFY(activate(title));
  QVERIFY(chooseMenu(title, title->rect().center(), QStringLiteral("Undo")));
  QCOMPARE(window->getLatestContent(), typed);
  QVERIFY(title->hasFocus());
  window->undo();
  QCOMPARE(window->getLatestContent().toUtf8(), source);
}

void TestTodoViewWindow::completionCheckmarkContrast() {
  qApp->setStyleSheet(m_theme->fetchQtStyleSheet());
  connect(m_theme.get(), &ThemeService::themeChanged, this,
          [this]() { qApp->setStyleSheet(m_theme->fetchQtStyleSheet()); });
  createArea();
  QVERIFY(openNote(QStringLiteral("checkmark.todo")).isValid());
  auto *window = current();
  const auto key = QStringLiteral("id:alpha");
  QVERIFY(window && openTask(window, key));
  auto *checked = control<QCheckBox>(window, "todoChecked");
  auto *title = control<QLineEdit>(window, "todoTitle");
  auto *details = control<QWidget>(window, "todoDetails");
  QVERIFY(checked && title && details);
  const auto source = window->getLatestContent();
  const auto luminance = [](const QColor &p_color) {
    const auto linear = [](double p_value) {
      return p_value <= 0.04045 ? p_value / 12.92 : std::pow((p_value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(p_color.redF()) + 0.7152 * linear(p_color.greenF()) +
           0.0722 * linear(p_color.blueF());
  };
  for (const auto &theme :
       {QStringLiteral("pure"), QStringLiteral("everforest-dark"), QStringLiteral("native")}) {
    m_theme->switchTheme(theme);
    QVERIFY(activate(title));
    movePointer(title, title->rect().center());
    // Composite the transparent checkbox over its actual themed parent, not an isolated grab.
    const auto background = details->grab().toImage();
    const auto center = checked->mapTo(details, checked->rect().center());
    const qreal scale = background.devicePixelRatio();
    const auto backgroundColor =
        background.pixelColor(qRound(center.x() * scale), qRound(center.y() * scale));
    const auto originalPalette = checked->palette();
    auto palette = originalPalette;
    // A native/stylesheet palette can provide no usable highlight contrast.
    palette.setColor(QPalette::Highlight, backgroundColor);
    palette.setColor(QPalette::HighlightedText, backgroundColor);
    checked->setPalette(palette);
    const auto uncheckedImage = details->grab().toImage();
    QTest::mouseClick(checked, Qt::LeftButton);
    QVERIFY(window->getController()->getDocument().findItem(key)->m_fields.m_checked);
    QVERIFY(activate(title));
    movePointer(title, title->rect().center());
    const auto checkedImage = details->grab().toImage();
    // Only the center quarter: the outline and outer keyboard focus ring cannot satisfy this.
    const int radius = qMin(checked->width(), checked->height()) / 8;
    const QRect interior(qRound((center.x() - radius) * scale),
                         qRound((center.y() - radius) * scale), qRound(2 * radius * scale),
                         qRound(2 * radius * scale));
    double contrast = 1;
    for (int y = interior.top(); y <= interior.bottom(); ++y) {
      for (int x = interior.left(); x <= interior.right(); ++x) {
        const auto before = luminance(uncheckedImage.pixelColor(x, y));
        const auto after = luminance(checkedImage.pixelColor(x, y));
        contrast = qMax(contrast, (qMax(before, after) + 0.05) / (qMin(before, after) + 0.05));
      }
    }
    QVERIFY2(contrast >= 4.5,
             qPrintable(QStringLiteral("%1 checkmark contrast: %2").arg(theme).arg(contrast)));
    window->undo();
    QVERIFY(!window->getController()->getDocument().findItem(key)->m_fields.m_checked);
    QCOMPARE(window->getLatestContent(), source);
    checked->setPalette(originalPalette);
  }
}

void TestTodoViewWindow::immediateNotesDatesAndSteps() {
  createArea();
  auto source = c_source;
  source.replace("[\"work\"]", "[\"work\",\"WORK\",\" spaced \" ]");
  QVERIFY(openNote(QStringLiteral("details.todo"), source).isValid());
  auto *window = current();
  QVERIFY(window && openTask(window, QStringLiteral("id:alpha")));
  auto *controller = window->getController();
  const auto fields = [&] {
    return controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields;
  };
  QCOMPARE(fields().m_tags, QStringList({QStringLiteral("work"), QStringLiteral("WORK"),
                                         QStringLiteral(" spaced ")}));
  auto *note = control<SimpleMarkdownEditor>(window, "todoNote");
  QVERIFY(note);
  QVERIFY(!note->document()->isUndoRedoEnabled());
  QVERIFY(activate(note->getTextEdit()));
  QString markdown = QStringLiteral("**Q4** and [proposal](proposal.md)\n\n- [ ] Note checklist");
  replaceDocument(note, markdown);
  QTRY_COMPARE(fields().m_note, markdown);
  QTest::keyClicks(note->getTextEdit(), " while typing");
  markdown += QStringLiteral(" while typing");
  QCOMPARE(fields().m_note, markdown);
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_steps.size(), 2);
  QCOMPARE(fields().m_tags.size(), 3); // Unrelated edits do not canonicalize imported tags.
  QVERIFY(window->getLatestContent().contains(QLatin1String("  > - [ ] Note checklist")));
  const int caret = note->getTextEdit()->textCursor().position();
  QCoreApplication::processEvents();
  QCOMPARE(note->getTextEdit()->textCursor().position(), caret);
  QVERIFY(note->getTextEdit()->hasFocus());
  QTest::keyClick(note->getTextEdit(), Qt::Key_Z, Qt::ControlModifier);
  QCOMPARE(fields().m_note, QStringLiteral("Existing **note**"));
  QCOMPARE(note->toPlainText(), QStringLiteral("Existing **note**"));
  QVERIFY(note->getTextEdit()->hasFocus());
  QTest::keyClick(note->getTextEdit(), Qt::Key_Y, Qt::ControlModifier);
  QCOMPARE(fields().m_note, markdown);
  auto *due = control<QPushButton>(window, "todoDue");
  auto *myDay = control<QPushButton>(window, "todoMyDay");
  QVERIFY(due && myDay);
  QVERIFY(chooseButtonMenu(due, "todoDueTomorrowAction"));
  QCOMPARE(fields().m_due, controller->getProjection().m_today.addDays(1));
  myDay->click();
  QCOMPARE(fields().m_myDay, controller->getProjection().m_today);
  QVERIFY(chooseButtonMenu(due, "todoClearDueAction"));
  QVERIFY(!fields().m_due.isValid());
  QCOMPARE(fields().m_myDay, controller->getProjection().m_today);
  auto *addStep = control<QLineEdit>(window, "todoAddStep");
  QVERIFY(typeText(addStep, QStringLiteral("Third step")));
  QTest::keyClick(addStep, Qt::Key_Return);
  auto *steps = control<QWidget>(window, "todoSteps");
  QVERIFY(steps);
  auto *stepTitle = control<QLineEdit>(steps, "todoStepTitle_2");
  auto *stepChecked = control<QCheckBox>(steps, "todoStepChecked_2");
  QVERIFY(stepTitle && stepChecked);
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_steps.size(), 3);
  QVERIFY(typeText(stepTitle, QStringLiteral("Renamed step")));
  stepChecked->setChecked(true);
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_steps.last().m_title,
           QStringLiteral("Renamed step"));
  QVERIFY(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_steps.last().m_checked);
  QVERIFY(!fields().m_checked);
  QVERIFY(chooseButtonMenu(control<QToolButton>(steps, "todoStepMore_2"), "todoDeleteStepAction"));
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_steps.size(), 2);
  window->undo();
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_steps.size(), 3);
  QCOMPARE(fields().m_tags, QStringList({QStringLiteral("work"), QStringLiteral("WORK"),
                                         QStringLiteral(" spaced ")}));
}

void TestTodoViewWindow::inlineStepTypingAndActions() {
  createArea();
  QVERIFY(openNote(QStringLiteral("inline-steps.todo")).isValid());
  auto *window = current();
  const auto key = QStringLiteral("id:alpha");
  QVERIFY(window && openTask(window, key));
  auto *controller = window->getController();
  const auto original = window->getLatestContent();
  QPointer<QLineEdit> title = control<QLineEdit>(window, "todoStepTitle_1");
  QVERIFY(typeText(title, QStringLiteral("Typed inline step")));
  QCOMPARE(control<QLineEdit>(window, "todoStepTitle_1"), title.data());
  QCOMPARE(title->cursorPosition(), int(QStringLiteral("Typed inline step").size()));
  QVERIFY(title->hasFocus());
  QCOMPARE(controller->getDocument().findItem(key)->m_steps[1].m_title,
           QStringLiteral("Typed inline step"));
  const auto typed = window->getLatestContent();
  QTest::keyClick(title, Qt::Key_Z, Qt::ControlModifier);
  QCOMPARE(window->getLatestContent(), original);
  QVERIFY(title && title->hasFocus());
  QCOMPARE(title->text(), QStringLiteral("Second step"));
  QTest::keyClick(title, Qt::Key_Y, Qt::ControlModifier);
  QCOMPARE(window->getLatestContent(), typed);
  QVERIFY(activate(control<QLineEdit>(window, "todoTitle")));
  QVERIFY(typeText(title, QStringLiteral("Separate typing group")));
  window->undo();
  QCOMPARE(window->getLatestContent(), typed);

  QVERIFY(chooseButtonMenu(control<QToolButton>(window, "todoStepMore_1"),
                           "todoToggleStepCompletedAction"));
  QVERIFY(controller->getDocument().findItem(key)->m_steps[1].m_checked);
  QVERIFY(!controller->getDocument().findItem(key)->m_fields.m_checked);
  auto *more = control<QToolButton>(window, "todoStepMore_1");
  QVERIFY(more && activate(more));
  QTest::mouseClick(more, Qt::LeftButton);
  QTRY_VERIFY(qobject_cast<QMenu *>(QApplication::activePopupWidget()));
  auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
  auto *promote = control<QAction>(menu, "todoPromoteStepAction");
  QVERIFY(!promote || !promote->isEnabled());
  QVERIFY(chooseOpenMenu("todoToggleStepCompletedAction"));
  QVERIFY(!controller->getDocument().findItem(key)->m_steps[1].m_checked);
  QCOMPARE(window->getLatestContent(), typed);

  QVERIFY(chooseButtonMenu(control<QToolButton>(window, "todoStepMore_0"), "todoDeleteStepAction"));
  QCOMPARE(controller->getDocument().findItem(key)->m_steps.size(), 1);
  QCOMPARE(controller->getDocument().findItem(key)->m_steps[0].m_title,
           QStringLiteral("Typed inline step"));
  window->undo();
  QCOMPARE(window->getLatestContent(), typed);

  const auto parentFields = controller->getDocument().findItem(key)->m_fields;
  const auto taskCount =
      controller->getDocument().findList(QStringLiteral("id:work"))->m_items.size();
  QVERIFY(
      chooseButtonMenu(control<QToolButton>(window, "todoStepMore_1"), "todoPromoteStepAction"));
  const auto *parent = controller->getDocument().findItem(key);
  QVERIFY(parent);
  QCOMPARE(parent->m_steps.size(), 1);
  QCOMPARE(parent->m_steps[0].m_title, QStringLiteral("First step"));
  QCOMPARE(parent->m_fields.m_note, parentFields.m_note);
  QCOMPARE(parent->m_fields.m_tags, parentFields.m_tags);
  QCOMPARE(controller->getSelectedItemKey(), key);
  const auto *list = controller->getDocument().findList(QStringLiteral("id:work"));
  QCOMPARE(list->m_items.size(), taskCount + 1);
  const auto &promoted = list->m_items.last();
  QCOMPARE(promoted.m_fields.m_title, QStringLiteral("Typed inline step"));
  QVERIFY(!promoted.m_id.isEmpty() && promoted.m_id != parent->m_id);
  QVERIFY(promoted.m_createdAt.isValid());
  QVERIFY(!promoted.m_fields.m_checked && !promoted.m_fields.m_important);
  QVERIFY(!promoted.m_fields.m_due.isValid() && !promoted.m_fields.m_myDay.isValid());
  QVERIFY(promoted.m_fields.m_note.isEmpty() && promoted.m_fields.m_tags.isEmpty());
  QVERIFY(promoted.m_steps.isEmpty());
  const auto promotedSource = window->getLatestContent();
  window->undo();
  QCOMPARE(window->getLatestContent(), typed);
  window->redo();
  QCOMPARE(window->getLatestContent(), promotedSource);
  QCOMPARE(controller->getSelectedItemKey(), key);
}

void TestTodoViewWindow::dueDateMenuAndPicker() {
  createArea();
  QVERIFY(openNote(QStringLiteral("due-picker.todo")).isValid());
  auto *window = current();
  QVERIFY(window && openTask(window, QStringLiteral("id:alpha")));
  auto *controller = window->getController();
  const auto dueDate = [&] {
    return controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields.m_due;
  };
  auto *due = control<QPushButton>(window, "todoDue");
  QVERIFY(due);
  const auto original = window->getLatestContent();
  const QDate today(2040, 2, 28);
  controller->setToday(today);
  const char *presets[] = {"todoDueTodayAction", "todoDueTomorrowAction", "todoDueNextWeekAction"};
  const int offsets[] = {0, 1, 7};
  for (int i = 0; i < 3; ++i) {
    QVERIFY(chooseButtonMenu(due, presets[i]));
    QCOMPARE(dueDate(), today.addDays(offsets[i]));
    window->undo();
    QCOMPARE(window->getLatestContent(), original);
  }
  controller->setToday(today.addDays(1));
  QVERIFY(chooseButtonMenu(due, "todoDueTodayAction"));
  QCOMPARE(dueDate(), today.addDays(1));
  QVERIFY(chooseButtonMenu(due, "todoClearDueAction"));
  QVERIFY(!dueDate().isValid());
  window->undo();
  window->undo();
  QCOMPARE(window->getLatestContent(), original);

  const auto openPicker = [&]() -> QDialog * {
    if (!chooseButtonMenu(due, "todoPickDueDateAction"))
      return nullptr;
    if (!QTest::qWaitFor([] {
          auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
          return dialog && dialog->objectName() == QLatin1String("todoDueDateDialog");
        }))
      return nullptr;
    return qobject_cast<QDialog *>(QApplication::activeModalWidget());
  };
  QPointer<QDialog> dialog = openPicker();
  const auto closePicker = qScopeGuard([&] {
    if (dialog)
      dialog->reject();
  });
  QVERIFY(dialog);
  auto *calendar = control<QCalendarWidget>(dialog, "todoDueCalendar");
  auto *cancel = control<QPushButton>(dialog, "todoDueDateCancel");
  QVERIFY(calendar && cancel);
  QCOMPARE(calendar->selectedDate(), today.addDays(1));
  const QDate picked(2041, 12, 31);
  calendar->setSelectedDate(picked);
  QCOMPARE(window->getLatestContent(), original);
  const auto undoSteps = window->getSourceEditor()->document()->availableUndoSteps();
  QTest::mouseClick(cancel, Qt::LeftButton);
  QTRY_VERIFY(!dialog || !dialog->isVisible());
  QCOMPARE(window->getLatestContent(), original);
  QCOMPARE(window->getSourceEditor()->document()->availableUndoSteps(), undoSteps);

  dialog = openPicker();
  QVERIFY(dialog);
  calendar = control<QCalendarWidget>(dialog, "todoDueCalendar");
  auto *accept = control<QPushButton>(dialog, "todoDueDateAccept");
  QVERIFY(calendar && accept);
  calendar->setSelectedDate(picked);
  QCOMPARE(window->getLatestContent(), original);
  QTest::mouseClick(accept, Qt::LeftButton);
  QTRY_VERIFY(!dialog || !dialog->isVisible());
  QCOMPARE(dueDate(), picked);
  const auto dated = window->getLatestContent();
  window->undo();
  QCOMPARE(window->getLatestContent(), original);
  window->redo();
  QCOMPARE(window->getLatestContent(), dated);

  dialog = openPicker();
  QVERIFY(dialog);
  calendar = control<QCalendarWidget>(dialog, "todoDueCalendar");
  cancel = control<QPushButton>(dialog, "todoDueDateCancel");
  QVERIFY(calendar && cancel);
  QCOMPARE(calendar->selectedDate(), picked);
  calendar->setSelectedDate(picked.addDays(10));
  QTest::mouseClick(cancel, Qt::LeftButton);
  QTRY_VERIFY(!dialog || !dialog->isVisible());
  QCOMPARE(window->getLatestContent(), dated);
  QVERIFY(chooseButtonMenu(due, "todoClearDueAction"));
  QVERIFY(!dueDate().isValid());
  window->undo();
  QCOMPARE(window->getLatestContent(), dated);
}

void TestTodoViewWindow::detailsGesturesRejectStaleOwner() {
  createArea();
  const auto filename = QStringLiteral("stale-details.todo");
  QVERIFY(openNote(filename).isValid());
  auto *window = current();
  QVERIFY(window);
  auto *controller = window->getController();
  // Reuse the isolated profile; each scenario restores its source and editability.
  for (const char *name :
       {"todoToggleStepCompletedAction", "todoPromoteStepAction", "todoDeleteStepAction",
        "todoDueTomorrowAction", "todoPickDueDateAction"}) {
    const auto actionName = QString::fromLatin1(name);
    for (int change = 0; change < 4; ++change) {
      qInfo() << "Stale details gesture:" << actionName << "transition:" << change;
      QVERIFY(openTask(window, QStringLiteral("id:alpha")));
      const bool picker = actionName == QLatin1String("todoPickDueDateAction");
      const bool step = actionName.contains(QLatin1String("Step"));
      QPointer<QAction> action;
      QPointer<QMenu> menu;
      QPointer<QDialog> dialog;
      const auto closePopup = qScopeGuard([&] {
        if (dialog)
          dialog->reject();
        if (menu)
          menu->close();
      });
      if (picker) {
        QVERIFY(chooseButtonMenu(control<QPushButton>(window, "todoDue"), "todoPickDueDateAction"));
        QTRY_VERIFY(qobject_cast<QDialog *>(QApplication::activeModalWidget()));
        dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        auto *calendar = control<QCalendarWidget>(dialog, "todoDueCalendar");
        QVERIFY(calendar);
        calendar->setSelectedDate(QDate(2040, 2, 29));
      } else {
        QAbstractButton *button =
            step ? static_cast<QAbstractButton *>(control<QToolButton>(window, "todoStepMore_1"))
                 : static_cast<QAbstractButton *>(control<QPushButton>(window, "todoDue"));
        QVERIFY(button && activate(button));
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(qobject_cast<QMenu *>(QApplication::activePopupWidget()));
        menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        action = menu->findChild<QAction *>(actionName);
        QVERIFY(action && action->isEnabled());
      }
      auto expected = c_source;
      if (change == 0) {
        expected.replace("Second step", "Foreign replacement step");
        QVERIFY(writeBytes(absolute(filename), expected));
        QVERIFY(window->reload());
      } else if (change == 1) {
        controller->setSelectedItemKey(QStringLiteral("id:beta"));
      } else if (change == 2) {
        window->setMode(ViewWindowMode::Read);
      } else {
        auto fields = controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields;
        fields.m_title = QStringLiteral("Changed while details gesture was open");
        QVERIFY(controller->updateTask(QStringLiteral("id:alpha"), fields));
        expected = window->getLatestContent().toUtf8();
      }
      const auto undoSteps = window->getSourceEditor()->document()->availableUndoSteps();
      if (picker && dialog) {
        // Even a queued acceptance must not authorize the obsolete gesture.
        dialog->accept();
      } else if (action) {
        action->trigger();
      }
      QCOMPARE(window->getLatestContent().toUtf8(), expected);
      QCOMPARE(window->getSourceEditor()->document()->availableUndoSteps(), undoSteps);
      QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_steps.size(), 2);
      QVERIFY(!controller->getDocument()
                   .findItem(QStringLiteral("id:alpha"))
                   ->m_fields.m_due.isValid());
      QVERIFY(
          !controller->getDocument().findItem(QStringLiteral("id:beta"))->m_fields.m_due.isValid());
      if (change != 3)
        QVERIFY(!window->isModified());
      if (dialog)
        dialog->reject();
      if (menu)
        menu->close();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      if (change == 0) {
        QVERIFY(writeBytes(absolute(filename), c_source));
        QVERIFY(window->reload());
      } else if (change == 2) {
        window->setMode(ViewWindowMode::Edit);
      } else if (change == 3) {
        window->undo();
      }
      QCOMPARE(window->getLatestContent().toUtf8(), c_source);
      if (change == 3)
        QVERIFY(window->save());
      QCOMPARE(readBytes(absolute(filename)), c_source);
      QVERIFY(!window->isModified());
    }
  }
}

void TestTodoViewWindow::filteringSortingAndDayRollover() {
  createArea();
  QVERIFY(openNote(QStringLiteral("filters.todo")).isValid());
  auto *window = current();
  QVERIFY(window && openTask(window, QStringLiteral("id:alpha")));
  auto *controller = window->getController();
  auto alpha = controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields;
  alpha.m_due = QDate(2026, 11, 13);
  alpha.m_myDay = QDate(2026, 11, 12);
  QVERIFY(controller->updateTask(QStringLiteral("id:alpha"), alpha));
  controller->setToday(QDate(2026, 11, 12));
  const auto before = window->getLatestContent();
  const auto revision = window->getSourceEditor()->document()->revision();
  const auto undoSteps = window->getSourceEditor()->document()->availableUndoSteps();
  QVERIFY(selectView(window, TodoViewKind::MyDay));
  QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Alpha")}));
  controller->setToday(QDate(2026, 11, 13));
  QVERIFY(taskTitles(window).isEmpty());
  QVERIFY(selectView(window, TodoViewKind::Planned));
  QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Alpha")}));
  QVERIFY(selectView(window, TodoViewKind::List, QStringLiteral("id:personal")));
  auto *search = control<QLineEdit>(window, "todoSearch");
  QVERIFY(typeText(search, QStringLiteral("First step")));
  QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Alpha")}));
  QVERIFY(openTask(window, QStringLiteral("id:alpha")));
  QVERIFY(typeText(search, QStringLiteral("Gamma")));
  QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Gamma")}));
  QCOMPARE(controller->getProjection().m_search, QStringLiteral("Gamma"));
  search->clear();
  QCOMPARE(controller->getProjection().m_view, TodoViewKind::List);
  QCOMPARE(controller->getProjection().m_listKey, QStringLiteral("id:personal"));
  QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Gamma")}));
  QVERIFY(selectView(window, TodoViewKind::All));
  QVERIFY(chooseSortMenu(window, "todoSortImportanceAction"));
  QCOMPARE(taskTitles(window).first(), QStringLiteral("Beta"));
  QVERIFY(!controller->getTaskModel()->isReorderingEnabled());
  QVERIFY(chooseSortMenu(window, "todoSortNameAction"));
  QCOMPARE(taskTitles(window),
           QStringList({QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gamma")}));
  QVERIFY(chooseSortMenu(window, "todoSortNameAction"));
  QCOMPARE(controller->getProjection().m_sort, TodoSortOrder::Manual);
  QVERIFY(selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  QVERIFY(controller->getTaskModel()->isReorderingEnabled());
  QVERIFY(selectView(window, TodoViewKind::All));
  QVERIFY(typeText(search, QStringLiteral("Alpha")));
  QVERIFY(chooseSortMenu(window, "todoSortMyDayAction"));
  QCOMPARE(controller->getProjection().m_search, QStringLiteral("Alpha"));
  QCOMPARE(controller->getProjection().m_view, TodoViewKind::All);
  QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Alpha")}));
  QVERIFY(typeText(search, QStringLiteral("Done")));
  auto *model = controller->getTaskModel();
  QCOMPARE(model->getActiveCount(), 0);
  QCOMPARE(model->getCompletedCount(), 1);
  QVERIFY(model->rowCount() > 0);
  const auto section = model->index(0);
  QVERIFY(section.data(TodoTaskModel::SectionRole).toBool());
  auto *tasks = control<QListView>(window, "todoTasks");
  tasks->setCurrentIndex(section);
  QVERIFY(activate(tasks));
  QTest::keyClick(tasks, Qt::Key_Return);
  QTRY_VERIFY(taskTitles(window).contains(QStringLiteral("Done")));
  QVERIFY(typeText(search, QStringLiteral("opaque-metadata-only")));
  QVERIFY(taskTitles(window).isEmpty());
  QCOMPARE(window->getLatestContent(), before);
  QCOMPARE(window->getSourceEditor()->document()->revision(), revision);
  QCOMPARE(window->getSourceEditor()->document()->availableUndoSteps(), undoSteps);
  QTimer *dayTimer = nullptr;
  for (auto *timer : window->findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly)) {
    if (timer->interval() == 60000)
      dayTimer = timer;
  }
  QVERIFY(dayTimer && dayTimer->isActive());
  controller->setToday(QDate::currentDate().addDays(-1));
  QVERIFY(QMetaObject::invokeMethod(dayTimer, "timeout", Qt::DirectConnection));
  QCOMPARE(controller->getProjection().m_today, QDate::currentDate());
  window->hide();
  QVERIFY(!dayTimer->isActive());
  controller->setToday(QDate::currentDate().addDays(-1));
  QVERIFY(QMetaObject::invokeMethod(dayTimer, "timeout", Qt::DirectConnection));
  QCOMPARE(controller->getProjection().m_today, QDate::currentDate().addDays(-1));
  window->show();
  QCOMPARE(controller->getProjection().m_today, QDate::currentDate());
  QCOMPARE(window->getLatestContent(), before);
  QCOMPARE(window->getSourceEditor()->document()->revision(), revision);
}

void TestTodoViewWindow::completedToggleKeepsNativeSelection_data() {
  QTest::addColumn<bool>("readOnly");
  QTest::addColumn<int>("width");
  QTest::newRow("editable-wide") << false << 1000;
  QTest::newRow("editable-narrow") << false << 600;
  QTest::newRow("readonly-wide") << true << 1000;
  QTest::newRow("readonly-narrow") << true << 600;
}

void TestTodoViewWindow::completedToggleKeepsNativeSelection() {
  QFETCH(bool, readOnly);
  QFETCH(int, width);
  createArea();
  QVERIFY(openNote(QStringLiteral("completed-focus.todo")).isValid());
  auto *window = current();
  QVERIFY(window && selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  m_area->resize(width, 760);
  auto *controller = window->getController();
  auto *model = controller->getTaskModel();
  auto *tasks = control<QListView>(window, "todoTasks");
  QVERIFY(tasks && activate(tasks));
  controller->setSelectedItemKey(QStringLiteral("id:alpha"));
  if (readOnly)
    window->setMode(ViewWindowMode::Read);
  const auto source = window->getLatestContent();
  const auto selectedTask = controller->getSelectedItemKey();
  const QPersistentModelIndex section(model->index(model->getActiveCount()));
  // Settle mode/model geometry before choosing the pointer's target row.
  QCoreApplication::processEvents();
  tasks->doItemsLayout();
  tasks->scrollTo(section);
  QCoreApplication::processEvents();
  movePointer(tasks->viewport(), tasks->visualRect(section).center());
  QTRY_COMPARE(tasks->viewport()->cursor().shape(), Qt::PointingHandCursor);
  QTest::mouseClick(tasks->viewport(), Qt::LeftButton, Qt::NoModifier,
                    tasks->visualRect(section).center());
  QVERIFY(controller->getProjection().m_completedExpanded);
  QCOMPARE(tasks->currentIndex(), QModelIndex(section));
  QCOMPARE(tasks->selectionModel()->selectedIndexes(), QModelIndexList{section});
  QVERIFY(tasks->hasFocus());
  for (const auto key : {Qt::Key_Return, Qt::Key_Space, Qt::Key_Enter}) {
    const bool expanded = controller->getProjection().m_completedExpanded;
    QTest::keyClick(tasks, key);
    QCOMPARE(controller->getProjection().m_completedExpanded, !expanded);
    QCOMPARE(tasks->currentIndex(), QModelIndex(section));
    QCOMPARE(tasks->selectionModel()->selectedIndexes(), QModelIndexList{section});
    QCOMPARE(controller->getSelectedItemKey(), selectedTask);
    QVERIFY(tasks->hasFocus());
  }
  QTest::mouseClick(tasks->viewport(), Qt::LeftButton, Qt::NoModifier,
                    tasks->visualRect(section).center());
  QCOMPARE(tasks->currentIndex(), QModelIndex(section));
  QVERIFY(tasks->hasFocus());
  const auto alpha = model->index(model->rowForKey(QStringLiteral("id:alpha")));
  movePointer(tasks->viewport(), tasks->visualRect(alpha).center());
  QTRY_COMPARE(tasks->viewport()->cursor().shape(), Qt::ArrowCursor);
  movePointer(tasks->viewport(),
              QPoint(tasks->viewport()->width() / 2, tasks->viewport()->height() - 8));
  QTRY_COMPARE(tasks->viewport()->cursor().shape(), Qt::ArrowCursor);
  movePointer(tasks->viewport(), tasks->visualRect(section).center());
  QTRY_COMPARE(tasks->viewport()->cursor().shape(), Qt::PointingHandCursor);
  controller->setSearch(QStringLiteral("Alpha"));
  QTRY_COMPARE(tasks->viewport()->cursor().shape(), Qt::ArrowCursor);
  controller->setSearch({});

  // Back and explicit file navigation still restore their requested task, not the section.
  controller->setCompletedExpanded(true);
  QVERIFY(openTask(window, QStringLiteral("id:done")));
  controller->setCompletedExpanded(false);
  window->getTodoEditor()->closeDetails();
  QCOMPARE(tasks->currentIndex().data(TodoTaskModel::IdRole).toString(), QStringLiteral("id:done"));
  QVERIFY(tasks->hasFocus());
  const auto currentSection = model->index(model->getActiveCount());
  QTest::mouseClick(tasks->viewport(), Qt::LeftButton, Qt::NoModifier,
                    tasks->visualRect(currentSection).center());
  QVERIFY(!controller->getProjection().m_completedExpanded);
  QCOMPARE(tasks->currentIndex(), currentSection);
  FileOpenSettings navigation;
  navigation.m_cursorOffset = source.indexOf(QStringLiteral("- [x] Done"));
  window->applyFileOpenSettings(navigation);
  QCOMPARE(tasks->currentIndex().data(TodoTaskModel::IdRole).toString(), QStringLiteral("id:done"));
  QVERIFY(tasks->hasFocus());
  QCOMPARE(window->getLatestContent(), source);
  QVERIFY(!window->isModified());
  QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
}

void TestTodoViewWindow::nativeMoveReorderAndDeletion() {
  createArea();
  QVERIFY(openNote(QStringLiteral("order.todo")).isValid());
  auto *window = current();
  QVERIFY(window && openTask(window, QStringLiteral("id:alpha")));
  auto *controller = window->getController();
  const auto original = *controller->getDocument().findItem(QStringLiteral("id:alpha"));
  QPointer<QMenu> menu = openTaskMenu(window, QStringLiteral("id:alpha"));
  const auto closePopup = qScopeGuard([&] {
    if (menu)
      menu->close();
  });
  QVERIFY(menu);
  // A delayed source layout update must not cancel an unchanged owner.
  QTextCursor sourceCursor(window->getSourceEditor()->document());
  QTextBlockFormat format;
  format.setLeftMargin(12);
  sourceCursor.mergeBlockFormat(format);
  QVERIFY(chooseOpenTaskDestination(QStringLiteral("id:personal")));
  {
    const auto *moved = controller->getDocument().findItem(QStringLiteral("id:alpha"));
    QVERIFY(moved);
    QCOMPARE(moved->m_listKey, QStringLiteral("id:personal"));
    QCOMPARE(moved->m_id, original.m_id);
    QCOMPARE(moved->m_createdAt, original.m_createdAt);
    QCOMPARE(moved->m_properties, original.m_properties);
    QCOMPARE(moved->m_fields.m_title, original.m_fields.m_title);
    QCOMPARE(moved->m_fields.m_checked, original.m_fields.m_checked);
    QCOMPARE(moved->m_fields.m_important, original.m_fields.m_important);
    QCOMPARE(moved->m_fields.m_myDay, original.m_fields.m_myDay);
    QCOMPARE(moved->m_fields.m_due, original.m_fields.m_due);
    QCOMPARE(moved->m_fields.m_tags, original.m_fields.m_tags);
    QCOMPARE(moved->m_fields.m_note, original.m_fields.m_note);
    QCOMPARE(moved->m_steps.size(), original.m_steps.size());
    for (int i = 0; i < original.m_steps.size(); ++i) {
      QCOMPARE(moved->m_steps[i].m_title, original.m_steps[i].m_title);
      QCOMPARE(moved->m_steps[i].m_checked, original.m_steps[i].m_checked);
    }
  }
  QVERIFY(selectView(window, TodoViewKind::List, QStringLiteral("id:personal")));
  auto *tasks = control<QListView>(window, "todoTasks");
  auto *model = controller->getTaskModel();
  QVERIFY(tasks && model->isReorderingEnabled());
  tasks->setCurrentIndex(model->index(model->rowForKey(QStringLiteral("id:alpha"))));
  QVERIFY(activate(tasks));
  QTest::keyClick(tasks, Qt::Key_Up, Qt::AltModifier);
  QCOMPARE(controller->getDocument().findList(QStringLiteral("id:personal"))->m_items.first().m_key,
           QStringLiteral("id:alpha"));
  std::unique_ptr<QMimeData> drag(
      model->mimeData({model->index(model->rowForKey(QStringLiteral("id:gamma")))}));
  QVERIFY(drag);
  QVERIFY(model->canDropMimeData(drag.get(), Qt::MoveAction, 0, 0, QModelIndex()));
  QVERIFY(model->dropMimeData(drag.get(), Qt::MoveAction, 0, 0, QModelIndex()));
  QCOMPARE(controller->getDocument().findList(QStringLiteral("id:personal"))->m_items.first().m_key,
           QStringLiteral("id:gamma"));
  window->undo();
  QCOMPARE(controller->getDocument().findList(QStringLiteral("id:personal"))->m_items.first().m_key,
           QStringLiteral("id:alpha"));
  QVERIFY(openTask(window, QStringLiteral("id:alpha")));
  bool named = false;
  {
    DialogAnswers answer({QMessageBox::Yes}, [&](QMessageBox *box) {
      named = box->text().contains(QStringLiteral("Alpha"));
    });
    QVERIFY(chooseTaskMenu(window, QStringLiteral("id:alpha"), "todoDeleteTaskAction"));
    QVERIFY(answer.accepted());
  }
  QVERIFY(named);
  QVERIFY(!controller->getDocument().findItem(QStringLiteral("id:alpha")));
  window->undo();
  QVERIFY(controller->getDocument().findItem(QStringLiteral("id:alpha")));
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields.m_note,
           original.m_fields.m_note);
}

void TestTodoViewWindow::taskMenuTogglesAndDueDates() {
  createArea();
  QVERIFY(openNote(QStringLiteral("task-menu-fields.todo")).isValid());
  auto *window = current();
  const auto key = QStringLiteral("id:alpha");
  QVERIFY(window && selectView(window, TodoViewKind::All));
  auto *controller = window->getController();
  const QDate today(2040, 2, 28);
  controller->setToday(today);
  controller->setCompletedExpanded(true);
  const auto original = window->getLatestContent();
  const auto fields = [&] { return controller->getDocument().findItem(key)->m_fields; };
  const char *toggles[] = {"todoToggleMyDayAction", "todoToggleImportantAction",
                           "todoToggleCompletedAction"};
  for (int i = 0; i < 3; ++i) {
    QVERIFY(chooseTaskMenu(window, key, toggles[i]));
    QCOMPARE(fields().m_myDay, i == 0 ? today : QDate());
    QCOMPARE(fields().m_important, i == 1);
    QCOMPARE(fields().m_checked, i == 2);
    const auto enabled = window->getLatestContent();
    QVERIFY(chooseTaskMenu(window, key, toggles[i]));
    QVERIFY(!fields().m_myDay.isValid());
    QVERIFY(!fields().m_important);
    QVERIFY(!fields().m_checked);
    window->undo();
    QCOMPARE(window->getLatestContent(), enabled);
    window->undo();
    QCOMPARE(window->getLatestContent(), original);
  }

  auto pastDay = fields();
  pastDay.m_myDay = today.addDays(-1);
  QVERIFY(controller->updateTask(key, pastDay));
  const auto pastSource = window->getLatestContent();
  QVERIFY(chooseTaskMenu(window, key, "todoToggleMyDayAction"));
  QCOMPARE(fields().m_myDay, today);
  window->undo();
  QCOMPARE(window->getLatestContent(), pastSource);
  window->undo();
  QCOMPARE(window->getLatestContent(), original);

  const char *presets[] = {"todoDueTodayAction", "todoDueTomorrowAction"};
  for (int i = 0; i < 2; ++i) {
    QVERIFY(chooseTaskMenu(window, key, presets[i]));
    QCOMPARE(fields().m_due, today.addDays(i));
    window->undo();
    QCOMPARE(window->getLatestContent(), original);
  }

  QPointer<QDialog> dialog;
  const auto closePicker = qScopeGuard([&] {
    if (dialog)
      dialog->reject();
  });
  const QDate picked(2041, 12, 31);
  for (const bool accept : {false, true}) {
    QVERIFY(chooseTaskMenu(window, key, "todoPickDueDateAction"));
    QTRY_VERIFY(qobject_cast<QDialog *>(QApplication::activeModalWidget()));
    dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    auto *calendar = control<QCalendarWidget>(dialog, "todoDueCalendar");
    auto *button = control<QPushButton>(dialog, accept ? "todoDueDateAccept" : "todoDueDateCancel");
    QVERIFY(calendar && button);
    QCOMPARE(calendar->selectedDate(), today);
    calendar->setSelectedDate(picked);
    QCOMPARE(window->getLatestContent(), original);
    const auto undoSteps = window->getSourceEditor()->document()->availableUndoSteps();
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY(!dialog || !dialog->isVisible());
    if (accept) {
      QCOMPARE(fields().m_due, picked);
      window->undo();
    } else {
      QCOMPARE(window->getSourceEditor()->document()->availableUndoSteps(), undoSteps);
    }
    QCOMPARE(window->getLatestContent(), original);
  }
  for (const bool included : {false, true}) {
    const auto menuToday = QDate::currentDate();
    controller->setToday(menuToday);
    auto initial = fields();
    initial.m_myDay = included ? menuToday : menuToday.addDays(1);
    QVERIFY(controller->updateTask(key, initial));
    const auto offeredSource = window->getLatestContent();
    QPointer<QMenu> rolloverMenu = openTaskMenu(window, key);
    const auto closeMenu = qScopeGuard([&] {
      if (rolloverMenu)
        rolloverMenu->close();
    });
    QVERIFY(rolloverMenu);
    auto *action = control<QAction>(rolloverMenu, "todoToggleMyDayAction");
    QVERIFY(action);
    const auto undoSteps = window->getSourceEditor()->document()->availableUndoSteps();
    // Deliver the offered action after the projection's clock rolls over, without a new focus
    // event.
    controller->setToday(menuToday.addDays(1));
    action->trigger();
    if (included) {
      QVERIFY(!fields().m_myDay.isValid());
      window->undo();
      QCOMPARE(window->getLatestContent(), offeredSource);
    } else {
      QCOMPARE(fields().m_myDay, menuToday.addDays(1));
      QCOMPARE(window->getLatestContent(), offeredSource);
      QCOMPARE(window->getSourceEditor()->document()->availableUndoSteps(), undoSteps);
    }
    rolloverMenu->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    window->undo();
    QCOMPARE(window->getLatestContent(), original);
  }
}

void TestTodoViewWindow::taskMenuMoveOwnershipAndAdmission() {
  createArea();
  const auto source = c_source + "\n# Hidden & Empty\n<!-- todo-group: {\"id\":\"hidden\"} -->\n\n"
                                 "## Empty & Ready\n<!-- todo-list: {\"id\":\"empty\"} -->\n";
  QVERIFY(openNote(QStringLiteral("task-menu-move.todo"), source).isValid());
  auto *window = current();
  QVERIFY(window && selectView(window, TodoViewKind::All));
  auto *controller = window->getController();
  controller->setGroupExpanded(QStringLiteral("id:hidden"), false);
  QVERIFY(openTask(window, QStringLiteral("id:gamma")));
  QPointer<QMenu> menu = openTaskMenu(window, QStringLiteral("id:alpha"));
  const auto closePopup = qScopeGuard([&] {
    if (menu)
      menu->close();
  });
  QVERIFY(menu);
  QCOMPARE(controller->getSelectedItemKey(), QStringLiteral("id:alpha"));
  auto *move = control<QAction>(menu, "todoMoveTaskAction");
  QVERIFY(move && move->isEnabled() && move->menu());
  QStringList destinations;
  for (auto *action : move->menu()->actions())
    destinations.append(action->data().toString());
  destinations.sort();
  QCOMPARE(destinations, QStringList({QStringLiteral("id:empty"), QStringLiteral("id:personal")}));

  // An own edit/undo returns to the same gesture state; source revision alone is not ownership.
  auto changed = controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields;
  changed.m_title = QStringLiteral("Temporary own edit");
  QVERIFY(controller->updateTask(QStringLiteral("id:alpha"), changed));
  window->undo();
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  QCOMPARE(controller->getSelectedItemKey(), QStringLiteral("id:alpha"));
  QVERIFY(chooseOpenTaskDestination(QStringLiteral("id:empty")));
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_listKey,
           QStringLiteral("id:empty"));
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:beta"))->m_listKey,
           QStringLiteral("id:work"));
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:gamma"))->m_listKey,
           QStringLiteral("id:personal"));
  window->undo();
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  QVERIFY(window->save());
  QVERIFY(!window->isModified());

  window->setMode(ViewWindowMode::Read);
  menu = openTaskMenu(window, QStringLiteral("id:alpha"));
  QVERIFY(menu);
  for (const char *name :
       {"todoToggleMyDayAction", "todoToggleImportantAction", "todoToggleCompletedAction",
        "todoDueTodayAction", "todoDueTomorrowAction", "todoPickDueDateAction",
        "todoMoveTaskAction", "todoDeleteTaskAction"}) {
    auto *action = control<QAction>(menu, name);
    QVERIFY(action && !action->isEnabled());
  }
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  menu->close();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  const QByteArray singleList = "<!-- todo: {\"version\":1} -->\n\n"
                                "## Only list\n<!-- todo-list: {\"id\":\"only\"} -->\n\n"
                                "- [ ] Alone\n  <!-- todo-item: {\"id\":\"alone\"} -->\n";
  QVERIFY(openNote(QStringLiteral("task-menu-no-destination.todo"), singleList).isValid());
  window = current();
  QVERIFY(window);
  menu = openTaskMenu(window, QStringLiteral("id:alone"));
  QVERIFY(menu);
  move = control<QAction>(menu, "todoMoveTaskAction");
  QVERIFY(move && !move->isEnabled());
  QCOMPARE(window->getLatestContent().toUtf8(), singleList);
  QVERIFY(!window->isModified());
}

void TestTodoViewWindow::taskMenuGesturesRejectStaleOwner() {
  createArea();
  const auto filename = QStringLiteral("stale-task-menu.todo");
  const auto source = c_source + "\n# Other group\n<!-- todo-group: {\"id\":\"other\"} -->\n\n"
                                 "## Empty\n<!-- todo-list: {\"id\":\"empty\"} -->\n";
  QVERIFY(openNote(filename, source).isValid());
  auto *window = current();
  QVERIFY(window);
  auto *controller = window->getController();
  const auto key = QStringLiteral("id:alpha");
  for (const char *name :
       {"todoToggleMyDayAction", "todoToggleImportantAction", "todoToggleCompletedAction",
        "todoDueTodayAction", "todoDueTomorrowAction", "todoPickDueDateAction",
        "todoMoveTaskAction", "todoDeleteTaskAction"}) {
    for (int change = 0; change < 5; ++change) {
      qInfo() << "Stale task menu gesture:" << name << "transition:" << change;
      QVERIFY(selectView(window, TodoViewKind::All));
      controller->setSelectedGroupKey(QString());
      QPointer<QMenu> menu = openTaskMenu(window, key);
      QPointer<QDialog> dialog;
      const auto closePopup = qScopeGuard([&] {
        if (dialog)
          dialog->reject();
        if (menu)
          menu->close();
      });
      QVERIFY(menu);
      QPointer<QAction> action = control<QAction>(menu, name);
      QVERIFY(action && action->isEnabled());
      const bool picker = qstrcmp(name, "todoPickDueDateAction") == 0;
      if (picker) {
        QVERIFY(chooseOpenMenu(name));
        QTRY_VERIFY(qobject_cast<QDialog *>(QApplication::activeModalWidget()));
        dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        auto *calendar = control<QCalendarWidget>(dialog, "todoDueCalendar");
        QVERIFY(calendar);
        calendar->setSelectedDate(QDate(2040, 2, 29));
      } else if (qstrcmp(name, "todoMoveTaskAction") == 0) {
        QVERIFY(action->menu());
        auto *submenu = action->menu();
        action.clear();
        for (auto *destination : submenu->actions()) {
          if (destination->data().toString() == QLatin1String("id:personal"))
            action = destination;
        }
        QVERIFY(action && action->isEnabled());
      }

      auto expected = source;
      if (change == 0) {
        expected.replace("Alpha", "Foreign replacement Alpha");
        QVERIFY(writeBytes(absolute(filename), expected));
        QVERIFY(window->reload());
      } else if (change == 1) {
        controller->setSelectedItemKey(QStringLiteral("id:beta"));
      } else if (change == 2) {
        controller->setSelectedGroupKey(QStringLiteral("id:other"));
        QCOMPARE(controller->getSelectedItemKey(), key);
      } else if (change == 3) {
        window->setMode(ViewWindowMode::Read);
      } else {
        auto fields = controller->getDocument().findItem(key)->m_fields;
        fields.m_title = QStringLiteral("Changed while task menu was open");
        QVERIFY(controller->updateTask(key, fields));
        expected = window->getLatestContent().toUtf8();
      }
      const auto undoSteps = window->getSourceEditor()->document()->availableUndoSteps();
      bool prompted = false;
      {
        DialogAnswers unexpected({QMessageBox::No}, [&](QMessageBox *) { prompted = true; });
        if (picker && dialog)
          dialog->accept();
        else if (action)
          action->trigger();
      }
      QVERIFY(!prompted);
      QCOMPARE(window->getLatestContent().toUtf8(), expected);
      QCOMPARE(window->getSourceEditor()->document()->availableUndoSteps(), undoSteps);
      if (dialog)
        dialog->reject();
      if (menu)
        menu->close();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

      if (change == 0) {
        QVERIFY(writeBytes(absolute(filename), source));
        QVERIFY(window->reload());
      } else if (change == 3) {
        window->setMode(ViewWindowMode::Edit);
      } else if (change == 4) {
        window->undo();
        // Undo restores bytes, not necessarily the buffer's dirty flag.
        QVERIFY(window->save());
      }
      controller->setSelectedGroupKey(QString());
      QCOMPARE(window->getLatestContent().toUtf8(), source);
      QCOMPARE(readBytes(absolute(filename)), source);
      QVERIFY(!window->isModified());
    }
  }
}

void TestTodoViewWindow::taskMenuAndListShortcuts() {
  createArea();
  QVERIFY(openNote(QStringLiteral("task-menu-shortcuts.todo")).isValid());
  auto *window = current();
  QVERIFY(window && selectView(window, TodoViewKind::All));
  auto *controller = window->getController();
  auto *tasks = control<QListView>(window, "todoTasks");
  QVERIFY(tasks);
  const auto key = QStringLiteral("id:alpha");
  controller->setCompletedExpanded(true);
  const auto original = window->getLatestContent();
  const QKeySequence myDayKey(Qt::CTRL | Qt::Key_T);
  QList<QPointer<QShortcut>> disabledShortcuts;
  for (auto *shortcut : window->findChildren<QShortcut *>()) {
    if (shortcut->isEnabled() && shortcut->keys().contains(myDayKey)) {
      disabledShortcuts.append(shortcut);
      shortcut->setEnabled(false);
    }
  }
  const auto restoreShortcuts = qScopeGuard([&] {
    for (const auto &shortcut : disabledShortcuts) {
      if (shortcut)
        shortcut->setEnabled(true);
    }
  });
  // Declare the parent conflict explicitly; isolated profiles need not bind EditRead to Ctrl+T.
  QShortcut parentShortcut(myDayKey, window);
  parentShortcut.setContext(Qt::WidgetWithChildrenShortcut);
  QSignalSpy parentActivated(&parentShortcut, &QShortcut::activated);
  QSignalSpy parentAmbiguous(&parentShortcut, &QShortcut::activatedAmbiguously);
  QPointer<QMenu> menu;
  const auto closePopup = qScopeGuard([&] {
    if (menu)
      menu->close();
  });
  const auto press = [&](Qt::Key p_key, Qt::KeyboardModifiers p_modifiers, bool p_popup) {
    if (p_popup) {
      menu = openTaskMenu(window, key);
      if (!menu)
        return false;
      QTest::keyClick(menu, p_key, p_modifiers);
      return QTest::qWaitFor([&] { return !menu || !menu->isVisible(); });
    }
    if (!openTask(window, key) || !activate(tasks))
      return false;
    QTest::keyClick(tasks, p_key, p_modifiers);
    return true;
  };

  for (const bool popup : {false, true}) {
    for (const auto shortcut : {Qt::Key_T, Qt::Key_D}) {
      QVERIFY(press(shortcut, Qt::ControlModifier, popup));
      const auto enabled = controller->getDocument().findItem(key)->m_fields;
      QCOMPARE(enabled.m_myDay,
               shortcut == Qt::Key_T ? controller->getProjection().m_today : QDate());
      QCOMPARE(enabled.m_checked, shortcut == Qt::Key_D);
      const auto enabledSource = window->getLatestContent();
      QVERIFY(press(shortcut, Qt::ControlModifier, popup));
      QVERIFY(!controller->getDocument().findItem(key)->m_fields.m_myDay.isValid());
      QVERIFY(!controller->getDocument().findItem(key)->m_fields.m_checked);
      window->undo();
      QCOMPARE(window->getLatestContent(), enabledSource);
      window->undo();
      QCOMPARE(window->getLatestContent(), original);
      QCOMPARE(window->getMode(), ViewWindowMode::Edit);
      QCOMPARE(parentActivated.count(), 0);
      QCOMPARE(parentAmbiguous.count(), 0);
    }
    for (const auto response : {QMessageBox::No, QMessageBox::Yes}) {
      DialogAnswers answer({response});
      QVERIFY(press(Qt::Key_Delete, Qt::NoModifier, popup));
      QVERIFY(answer.accepted());
      if (response == QMessageBox::Yes) {
        QVERIFY(!controller->getDocument().findItem(key));
        window->undo();
      } else {
        QVERIFY(controller->getDocument().findItem(key));
      }
      QCOMPARE(window->getLatestContent(), original);
    }
  }

  const auto pressAll = [&] {
    QTest::keyClick(tasks, Qt::Key_T, Qt::ControlModifier);
    QTest::keyClick(tasks, Qt::Key_D, Qt::ControlModifier);
    QTest::keyClick(tasks, Qt::Key_Delete);
  };
  QVERIFY(openTask(window, key) && activate(tasks));
  tasks->setCurrentIndex(
      controller->getTaskModel()->index(controller->getTaskModel()->getActiveCount()));
  QVERIFY(tasks->currentIndex().data(TodoTaskModel::SectionRole).toBool());
  {
    bool prompted = false;
    DialogAnswers unexpected({QMessageBox::No}, [&](QMessageBox *) { prompted = true; });
    pressAll();
    QVERIFY(!prompted);
  }
  QCOMPARE(window->getLatestContent(), original);
  tasks->setCurrentIndex(QModelIndex());
  {
    bool prompted = false;
    DialogAnswers unexpected({QMessageBox::No}, [&](QMessageBox *) { prompted = true; });
    pressAll();
    QVERIFY(!prompted);
  }
  QCOMPARE(window->getLatestContent(), original);
  QVERIFY(window->save());
  window->setMode(ViewWindowMode::Read);
  QVERIFY(openTask(window, key) && activate(tasks));
  {
    bool prompted = false;
    DialogAnswers unexpected({QMessageBox::No}, [&](QMessageBox *) { prompted = true; });
    pressAll();
    QVERIFY(!prompted);
  }
  QCOMPARE(window->getLatestContent(), original);
  QCOMPARE(window->getMode(), ViewWindowMode::Read);
  QCOMPARE(parentActivated.count(), 0);
  QCOMPARE(parentAmbiguous.count(), 0);
  QVERIFY(!window->isModified());
}

void TestTodoViewWindow::taskShortcutsStayScoped() {
  createArea();
  QVERIFY(openNote(QStringLiteral("task-shortcut-scope.todo")).isValid());
  auto *window = current();
  const auto key = QStringLiteral("id:alpha");
  QVERIFY(window && openTask(window, key));
  auto *controller = window->getController();
  auto *tasks = control<QListView>(window, "todoTasks");
  QVERIFY(tasks);
  const auto original = window->getLatestContent();
  const QKeySequence myDayKey(Qt::CTRL | Qt::Key_T);
  QList<QPointer<QShortcut>> disabledShortcuts;
  for (auto *shortcut : window->findChildren<QShortcut *>()) {
    if (shortcut->isEnabled() && shortcut->keys().contains(myDayKey)) {
      disabledShortcuts.append(shortcut);
      shortcut->setEnabled(false);
    }
  }
  const auto restoreShortcuts = qScopeGuard([&] {
    for (const auto &shortcut : disabledShortcuts) {
      if (shortcut)
        shortcut->setEnabled(true);
    }
  });
  // Declare the parent conflict explicitly; isolated profiles need not bind EditRead to Ctrl+T.
  QShortcut parentShortcut(myDayKey, window);
  parentShortcut.setContext(Qt::WidgetWithChildrenShortcut);
  QSignalSpy parentActivated(&parentShortcut, &QShortcut::activated);
  QSignalSpy parentAmbiguous(&parentShortcut, &QShortcut::activatedAmbiguously);
  QVERIFY(activate(tasks));
  QTest::keyClick(tasks, Qt::Key_T, Qt::ControlModifier);
  QCOMPARE(controller->getDocument().findItem(key)->m_fields.m_myDay,
           controller->getProjection().m_today);
  QCOMPARE(parentActivated.count(), 0);
  window->undo();
  QCOMPARE(window->getLatestContent(), original);
  QPointer<QMenu> menu = openTaskMenu(window, key);
  QPointer<QDialog> dialog;
  const auto closePopup = qScopeGuard([&] {
    if (dialog)
      dialog->reject();
    if (menu)
      menu->close();
  });
  QVERIFY(menu);
  QTest::keyClick(menu, Qt::Key_T, Qt::ControlModifier);
  QTRY_VERIFY(!menu || !menu->isVisible());
  QCOMPARE(controller->getDocument().findItem(key)->m_fields.m_myDay,
           controller->getProjection().m_today);
  QCOMPARE(parentActivated.count(), 0);
  window->undo();
  QCOMPARE(window->getLatestContent(), original);

  menu = openTaskMenu(window, key);
  QVERIFY(menu);
  auto *move = control<QAction>(menu, "todoMoveTaskAction");
  QVERIFY(move && move->isEnabled() && move->menu());
  QPointer<QMenu> submenu = move->menu();
  menu->setActiveAction(move);
  QTest::keyClick(menu, Qt::Key_Right);
  QTRY_VERIFY(submenu && submenu->isVisible());
  QTest::keyClick(submenu, Qt::Key_T, Qt::ControlModifier);
  QTRY_VERIFY(!submenu || !submenu->isVisible());
  QTRY_VERIFY(!menu || !menu->isVisible());
  QCOMPARE(controller->getDocument().findItem(key)->m_fields.m_myDay,
           controller->getProjection().m_today);
  QCOMPARE(parentActivated.count(), 0);
  window->undo();
  QCOMPARE(window->getLatestContent(), original);

  for (const char *name : {"todoTitle", "todoStepTitle_1", "todoSearch", "todoAddTask"}) {
    QVERIFY(openTask(window, key));
    auto *field = control<QLineEdit>(window, name);
    QVERIFY(field && activate(field));
    field->deselect();
    field->setCursorPosition(field->text().size());
    const int activations = parentActivated.count();
    QTest::keyClick(field, Qt::Key_T, Qt::ControlModifier);
    QCOMPARE(parentActivated.count(), activations + 1);
    QTest::keyClick(field, Qt::Key_D, Qt::ControlModifier);
    bool prompted = false;
    {
      DialogAnswers unexpected({QMessageBox::No}, [&](QMessageBox *) { prompted = true; });
      QTest::keyClick(field, Qt::Key_Delete);
    }
    QVERIFY(!prompted);
    QVERIFY(controller->getDocument().findItem(key));
    QVERIFY(!controller->getDocument().findItem(key)->m_fields.m_myDay.isValid());
    QVERIFY(!controller->getDocument().findItem(key)->m_fields.m_checked);
    QCOMPARE(window->getLatestContent(), original);
  }

  QVERIFY(openTask(window, key));
  auto *note = control<SimpleMarkdownEditor>(window, "todoNote");
  QVERIFY(note && activate(note->getTextEdit()));
  auto cursor = note->getTextEdit()->textCursor();
  cursor.movePosition(QTextCursor::End);
  note->getTextEdit()->setTextCursor(cursor);
  const auto beforeNoteShortcut = parentActivated.count();
  QTest::keyClick(note->getTextEdit(), Qt::Key_T, Qt::ControlModifier);
  QCOMPARE(parentActivated.count(), beforeNoteShortcut + 1);
  QTest::keyClick(note->getTextEdit(), Qt::Key_D, Qt::ControlModifier);
  {
    bool prompted = false;
    DialogAnswers unexpected({QMessageBox::No}, [&](QMessageBox *) { prompted = true; });
    QTest::keyClick(note->getTextEdit(), Qt::Key_Delete);
    QVERIFY(!prompted);
  }
  QVERIFY(controller->getDocument().findItem(key));
  QVERIFY(!controller->getDocument().findItem(key)->m_fields.m_myDay.isValid());
  QVERIFY(!controller->getDocument().findItem(key)->m_fields.m_checked);

  QVERIFY(selectView(window, TodoViewKind::All));
  auto *lists = control<QListView>(window, "todoLists");
  QVERIFY(lists && activate(lists));
  const auto beforeSidebarShortcut = parentActivated.count();
  const auto beforeSidebar = window->getLatestContent();
  QTest::keyClick(lists, Qt::Key_T, Qt::ControlModifier);
  QCOMPARE(parentActivated.count(), beforeSidebarShortcut + 1);
  QTest::keyClick(lists, Qt::Key_D, Qt::ControlModifier);
  {
    bool prompted = false;
    DialogAnswers unexpected({QMessageBox::No}, [&](QMessageBox *) { prompted = true; });
    QTest::keyClick(lists, Qt::Key_Delete);
    QVERIFY(!prompted);
  }
  QCOMPARE(window->getLatestContent(), beforeSidebar);

  QVERIFY(chooseTaskMenu(window, key, "todoPickDueDateAction"));
  QTRY_VERIFY(qobject_cast<QDialog *>(QApplication::activeModalWidget()));
  dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
  auto *calendar = control<QCalendarWidget>(dialog, "todoDueCalendar");
  QVERIFY(calendar && activate(calendar));
  const auto beforeModal = window->getLatestContent();
  const auto beforeModalShortcut = parentActivated.count();
  QTest::keyClick(calendar, Qt::Key_T, Qt::ControlModifier);
  QTest::keyClick(calendar, Qt::Key_D, Qt::ControlModifier);
  {
    bool prompted = false;
    DialogAnswers unexpected({QMessageBox::No}, [&](QMessageBox *) { prompted = true; });
    QTest::keyClick(calendar, Qt::Key_Delete);
    QVERIFY(!prompted);
  }
  QCOMPARE(window->getLatestContent(), beforeModal);
  QCOMPARE(parentActivated.count(), beforeModalShortcut);
  QCOMPARE(parentAmbiguous.count(), 0);
  QVERIFY(dialog && dialog->isVisible());
  auto *cancel = control<QPushButton>(dialog, "todoDueDateCancel");
  QVERIFY(cancel);
  QTest::mouseClick(cancel, Qt::LeftButton);
  QTRY_VERIFY(!dialog || !dialog->isVisible());
}

void TestTodoViewWindow::listIconsPersistAndUndo() {
  createArea();
  const auto buffer = openNote(QStringLiteral("icons.todo"));
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window && selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  auto *button = control<QPushButton>(window, "todoListIcon");
  QVERIFY(button && button->isVisible() && button->isEnabled());
  const auto original = window->getLatestContent();
  QVERIFY(answerDialog(
      "todoListIconDialog",
      [](QDialog *p_dialog) {
        auto *input = control<QLineEdit>(p_dialog, "todoListIconSearch");
        if (!input || !typeText(input, QStringLiteral("rocket")))
          return false;
        QTest::keyClick(input, Qt::Key_Escape);
        return p_dialog->result() == QDialog::Rejected;
      },
      [&] { button->click(); }));
  QCOMPARE(window->getLatestContent(), original);
  QVERIFY(!window->isModified());
  QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
  QVERIFY(answerDialog(
      "todoListIconDialog",
      [](QDialog *p_dialog) { return pickListIcon(p_dialog, QStringLiteral("rocket")); },
      [&] { button->click(); }));
  const auto rocket = QString::fromUtf8("🚀");
  QCOMPARE(button->text(), rocket);
  const auto assigned = window->getLatestContent();
  window->undo();
  QCOMPARE(window->getLatestContent(), original);
  QVERIFY(button->text().isEmpty());
  window->redo();
  QCOMPARE(window->getLatestContent(), assigned);
  QCOMPARE(button->text(), rocket);
  QVERIFY(window->save());
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()));
  QCOMPARE(readBytes(absolute(QStringLiteral("icons.todo"))), assigned.toUtf8());
  const auto node = buffer.nodeId();
  QVERIFY(m_area->getController()->closeViewWindow(window->getViewWindowId(), false));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(m_buffers->openBuffer(node).isValid());
  window = current();
  QVERIFY(window && selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  button = control<QPushButton>(window, "todoListIcon");
  QVERIFY(button);
  QCOMPARE(button->text(), rocket);
  QCOMPARE(window->getLatestContent(), assigned);
  QVERIFY(!window->isModified());
  QVERIFY(answerDialog(
      "todoListIconDialog",
      [](QDialog *p_dialog) { return pickListIcon(p_dialog, QStringLiteral("briefcase"), true); },
      [&] { button->click(); }));
  QCOMPARE(button->text(), QString::fromUtf8("💼"));
  window->undo();
  QCOMPARE(button->text(), rocket);
  window->setMode(ViewWindowMode::Read);
  QVERIFY(button->isVisible() && !button->isEnabled());
  QCOMPARE(button->text(), rocket);
  window->setMode(ViewWindowMode::Edit);
  QVERIFY(answerDialog(
      "todoListIconDialog",
      [](QDialog *p_dialog) {
        auto *remove = control<QPushButton>(p_dialog, "todoRemoveListIcon");
        if (!remove || !remove->isEnabled())
          return false;
        remove->click();
        return p_dialog->result() == QDialog::Accepted;
      },
      [&] { button->click(); }));
  QCOMPARE(window->getLatestContent(), original);
  QVERIFY(button->text().isEmpty());
  window->undo();
  QCOMPARE(window->getLatestContent(), assigned);
  QCOMPARE(button->text(), rocket);
  QVERIFY(selectView(window, TodoViewKind::All));
  QVERIFY(button->isHidden());
}

void TestTodoViewWindow::listIconPickerRejectsStaleOwner_data() {
  QTest::addColumn<int>("change");
  QTest::newRow("reload") << 0;
  QTest::newRow("list-selection") << 1;
  QTest::newRow("read-only") << 2;
}

void TestTodoViewWindow::listIconPickerRejectsStaleOwner() {
  QFETCH(int, change);
  createArea();
  QVERIFY(openNote(QStringLiteral("stale-icon.todo")).isValid());
  auto *window = current();
  QVERIFY(window && selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  auto *button = control<QPushButton>(window, "todoListIcon");
  QVERIFY(button);
  auto expected = c_source;
  QVERIFY(answerDialog(
      "todoListIconDialog",
      [&](QDialog *p_dialog) {
        if (change == 0) {
          expected.replace("## Work", "## Reloaded");
          if (!writeBytes(absolute(QStringLiteral("stale-icon.todo")), expected) ||
              !window->reload())
            return false;
        } else if (change == 1) {
          window->getController()->setView(TodoViewKind::List, QStringLiteral("id:personal"));
        } else {
          window->setMode(ViewWindowMode::Read);
        }
        return pickListIcon(p_dialog, QStringLiteral("rocket"));
      },
      [&] { button->click(); }));
  QCOMPARE(window->getLatestContent().toUtf8(), expected);
  QVERIFY(!window->isModified());
  QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
}

void TestTodoViewWindow::nativeListActions() {
  createArea();
  QVERIFY(openNote(QStringLiteral("lists.todo")).isValid());
  auto *window = current();
  QVERIFY(window);
  auto *controller = window->getController();
  auto *lists = control<QListView>(window, "todoLists");
  QVERIFY(lists);
  const auto listMenu = [&](const QString &key, const QString &label) {
    const auto index =
        controller->getListModel()->index(controller->getListModel()->rowForKey(key));
    if (!index.isValid() || !activate(lists))
      return false;
    lists->scrollTo(index);
    return chooseMenu(lists->viewport(), lists->visualRect(index).center(), label);
  };
  QVERIFY(listMenu(QStringLiteral("id:work"), QStringLiteral("Rename")));
  auto *title = control<QLineEdit>(window, "todoListTitle");
  QVERIFY(title && title->hasFocus());
  QVERIFY(typeText(title, QStringLiteral("Work renamed")));
  QCOMPARE(controller->getDocument().findList(QStringLiteral("id:work"))->m_title,
           QStringLiteral("Work renamed"));
  window->undo();
  QCOMPARE(controller->getDocument().findList(QStringLiteral("id:work"))->m_title,
           QStringLiteral("Work"));
  window->redo();
  auto *description = control<QLabel>(window, "todoListDescription");
  QVERIFY(description && description->isVisible());
  QCOMPARE(description->text(), QStringLiteral("Team commitments"));
  const auto beforeDescription = window->getLatestContent();
  const QString draft = QStringLiteral("<b>Plain text</b> **Markdown**\n\n> Nested quote");
  auto *more = control<QPushButton>(window, "todoListMore");
  bool chosen = false;
  const auto editDescription = [&](const QString &p_initial, const QString &p_text,
                                   QDialogButtonBox::StandardButton p_answer,
                                   const std::function<void()> &p_open) {
    return answerDialog(
        "todoListDescriptionDialog",
        [&](QDialog *p_dialog) {
          auto *input = control<PlainTextEdit>(p_dialog, "todoDescriptionTextEdit");
          auto *buttons = p_dialog->findChild<QDialogButtonBox *>();
          if (!input || !buttons || input->toPlainText() != p_initial || !activate(input))
            return false;
          input->selectAll();
          input->insertPlainText(p_text);
          QTest::keyClick(input, Qt::Key_Z, Qt::ControlModifier);
          if (input->toPlainText() != p_initial)
            return false;
          input->redo();
          if (input->toPlainText() != p_text)
            return false;
          QTest::mouseClick(buttons->button(p_answer), Qt::LeftButton);
          return true;
        },
        p_open);
  };
  const auto openMore = [&] { chosen = chooseButtonMenu(more, "todoEditListDescriptionAction"); };
  QVERIFY(editDescription(description->text(), draft, QDialogButtonBox::Cancel, openMore));
  QVERIFY(chosen);
  QCOMPARE(window->getLatestContent(), beforeDescription);
  QCOMPARE(description->text(), QStringLiteral("Team commitments"));
  QVERIFY(editDescription(description->text(), draft, QDialogButtonBox::Ok, [&] {
    chosen = listMenu(QStringLiteral("id:work"), QStringLiteral("Edit Description"));
  }));
  QVERIFY(chosen);
  QCOMPARE(controller->getDocument().findList(QStringLiteral("id:work"))->m_description, draft);
  QCOMPARE(description->text(), draft);
  window->undo();
  QCOMPARE(window->getLatestContent(), beforeDescription);
  QCOMPARE(description->text(), QStringLiteral("Team commitments"));
  window->redo();
  QCOMPARE(description->text(), draft);
  QVERIFY(editDescription(draft, {}, QDialogButtonBox::Ok, openMore));
  QVERIFY(chosen);
  QVERIFY(description->isHidden());
  QVERIFY(controller->getDocument().findList(QStringLiteral("id:work"))->m_description.isEmpty());
  window->undo();
  QVERIFY(description->isVisible());
  QCOMPARE(description->text(), draft);

  // A context-menu target need not be the list currently displayed.
  QVERIFY(editDescription({}, QStringLiteral("Personal description"), QDialogButtonBox::Ok, [&] {
    chosen = listMenu(QStringLiteral("id:personal"), QStringLiteral("Edit Description"));
  }));
  QVERIFY(chosen);
  QCOMPARE(controller->getDocument().findList(QStringLiteral("id:personal"))->m_description,
           QStringLiteral("Personal description"));
  QCOMPARE(controller->getDocument().findList(QStringLiteral("id:work"))->m_description, draft);
  QVERIFY(selectView(window, TodoViewKind::List, QStringLiteral("id:personal")));
  QCOMPARE(description->text(), QStringLiteral("Personal description"));
  QVERIFY(description->isVisible());
  QVERIFY(selectView(window, TodoViewKind::All));
  QVERIFY(description->isHidden());
  QVERIFY(selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  QVERIFY(activate(lists));
  lists->setCurrentIndex(controller->getListModel()->index(
      controller->getListModel()->rowForKey(QStringLiteral("id:work"))));
  QTest::keyClick(lists, Qt::Key_Down, Qt::AltModifier);
  QCOMPARE(controller->getDocument().m_lists.last().m_key, QStringLiteral("id:work"));
  QTest::keyClick(lists, Qt::Key_Up, Qt::AltModifier);
  QCOMPARE(controller->getDocument().m_lists.first().m_key, QStringLiteral("id:work"));
  bool named = false;
  {
    DialogAnswers dialog({QMessageBox::No}, [&](QMessageBox *box) {
      named = box->text().contains(QStringLiteral("Work renamed"));
    });
    QVERIFY(listMenu(QStringLiteral("id:work"), QStringLiteral("Delete")));
    QVERIFY(dialog.accepted());
  }
  QVERIFY(named);
  QVERIFY(controller->getDocument().findList(QStringLiteral("id:work")));
  const auto beforeDelete = window->getLatestContent();
  {
    DialogAnswers dialog({QMessageBox::Yes});
    QVERIFY(listMenu(QStringLiteral("id:work"), QStringLiteral("Delete")));
    QVERIFY(dialog.accepted());
  }
  QVERIFY(!controller->getDocument().findList(QStringLiteral("id:work")));
  QVERIFY(!controller->getDocument().findItem(QStringLiteral("id:alpha")));
  QVERIFY(!window->getLatestContent().contains(QLatin1String("Work-scoped separator")));
  QVERIFY(window->getLatestContent().contains(QLatin1String("File-scoped author comment")));
  window->undo();
  QCOMPARE(window->getLatestContent(), beforeDelete);
  auto *newList = control<QPushButton>(window, "todoNewList");
  QVERIFY(newList);
  QVERIFY(answerNewList(QStringLiteral("Empty"), [&] { newList->click(); }));
  const auto emptyKey = controller->getProjection().m_listKey;
  QVERIFY(controller->getDocument().findList(emptyKey)->m_items.isEmpty());
  // An empty-list removal needs no modal confirmation but remains one undo step.
  QVERIFY(listMenu(emptyKey, QStringLiteral("Delete")));
  QVERIFY(!QApplication::activeModalWidget());
  QVERIFY(!controller->getDocument().findList(emptyKey));
  window->undo();
  QVERIFY(controller->getDocument().findList(emptyKey));
  const auto saved = window->getLatestContent();
  QVERIFY(window->save());
  const auto buffer = window->getBuffer();
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()));
  QCOMPARE(readBytes(absolute(QStringLiteral("lists.todo"))), saved.toUtf8());
  const auto node = buffer.nodeId();
  QVERIFY(m_area->getController()->closeViewWindow(window->getViewWindowId(), false));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(m_buffers->openBuffer(node).isValid());
  window = current();
  QVERIFY(window && selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  description = control<QLabel>(window, "todoListDescription");
  QVERIFY(description && description->isVisible());
  QCOMPARE(description->text(), draft);
  QCOMPARE(window->getLatestContent(), saved);
}

void TestTodoViewWindow::descriptionDialogRejectsStaleOwner_data() {
  QTest::addColumn<int>("change");
  QTest::newRow("foreign-reload") << 0;
  QTest::newRow("task-selection") << 1;
  QTest::newRow("read-only") << 2;
}

void TestTodoViewWindow::descriptionDialogRejectsStaleOwner() {
  QFETCH(int, change);
  createArea();
  QVERIFY(openNote(QStringLiteral("stale-description.todo")).isValid());
  auto *window = current();
  QVERIFY(window && selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  auto expected = c_source;
  bool chosen = false;
  QVERIFY(answerDialog(
      "todoListDescriptionDialog",
      [&](QDialog *p_dialog) {
        auto *input = control<PlainTextEdit>(p_dialog, "todoDescriptionTextEdit");
        if (!input)
          return false;
        input->setPlainText(QStringLiteral("Stale draft"));
        if (change == 0) {
          expected.replace("Team commitments", "Foreign description");
          if (!writeBytes(absolute(QStringLiteral("stale-description.todo")), expected) ||
              !window->reload())
            return false;
        } else if (change == 1) {
          window->getController()->setSelectedItemKey(QStringLiteral("id:beta"));
        } else {
          window->setMode(ViewWindowMode::Read);
        }
        p_dialog->accept();
        return true;
      },
      [&] {
        chosen = chooseButtonMenu(control<QPushButton>(window, "todoListMore"),
                                  "todoEditListDescriptionAction");
      }));
  QVERIFY(chosen);
  QCOMPARE(window->getLatestContent().toUtf8(), expected);
  QVERIFY(!window->isModified());
  QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
}

void TestTodoViewWindow::sidebarDropEvents_data() {
  QTest::addColumn<int>("width");
  QTest::addColumn<bool>("hasDefault");
  QTest::newRow("wide-existing-default") << 1000 << true;
  QTest::newRow("narrow-existing-default") << 600 << true;
  QTest::newRow("wide-create-default") << 1000 << false;
  QTest::newRow("narrow-create-default") << 600 << false;
}

void TestTodoViewWindow::sidebarDropEvents() {
  QFETCH(int, width);
  QFETCH(bool, hasDefault);
  const QByteArray source = (hasDefault ? c_source : QByteArray()) +
                            "\n# Projects\n<!-- todo-group: {\"id\":\"projects\"} -->\n\n"
                            "## Last\n<!-- todo-list: {\"id\":\"last\"} -->\n\n"
                            "- [ ] Keep\n  <!-- todo-item: {\"id\":\"keep\"} -->\n";
  createArea();
  QVERIFY(openNote(QStringLiteral("sidebar-drops.todo"), source).isValid());
  auto *window = current();
  QVERIFY(window);
  m_area->resize(width, 760);
  QVERIFY(showLists(window));
  auto *lists = control<QListView>(window, "todoLists");
  QVERIFY(activate(lists));
  auto *controller = window->getController();
  auto *model = controller->getListModel();
  const auto last = model->index(model->rowForKey(QStringLiteral("id:last")));
  lists->setCurrentIndex(last);
  lists->scrollTo(last);
  QCOMPARE(last.row(), model->rowCount() - 1);
  const QPoint empty(lists->viewport()->width() / 2, lists->viewport()->height() - 5);
  QVERIFY(!lists->indexAt(empty).isValid());
  QVERIFY(dropOnSidebar(lists, last, empty));
  const auto *moved = controller->getDocument().findList(QStringLiteral("id:last"));
  QVERIFY(moved && controller->getDocument().groupForList(*moved)->m_isDefault);
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:keep"))->m_fields.m_title,
           QStringLiteral("Keep"));
  window->undo();
  QCOMPARE(window->getLatestContent().toUtf8(), source);

  const auto firstGroup = model->index(model->rowForGroup(QStringLiteral("id:projects")));
  lists->scrollTo(firstGroup);
  const auto groupRect = lists->visualRect(firstGroup);
  QVERIFY(dropOnSidebar(lists, model->index(model->rowForKey(QStringLiteral("id:last"))),
                        QPoint(groupRect.center().x(), groupRect.top() + 1)));
  const auto *defaultGroup = controller->getDocument().defaultGroup();
  QVERIFY(defaultGroup);
  QCOMPARE(controller->getDocument()
               .groupForList(*controller->getDocument().findList(QStringLiteral("id:last")))
               ->m_key,
           defaultGroup->m_key);
  QCOMPARE(model->rowForGroup(defaultGroup->m_key), -1);
  QCOMPARE(model->rowForKey(QStringLiteral("id:last")), hasDefault ? 6 : 4);
  QVERIFY(model->index(model->rowForKey(QStringLiteral("id:last")))
              .data(TodoListModel::GroupIdRole)
              .toString()
              .isEmpty());
  window->undo();
  QCOMPARE(window->getLatestContent().toUtf8(), source);

  if (hasDefault) {
    auto work = model->index(model->rowForKey(QStringLiteral("id:work")));
    lists->scrollTo(work);
    QVERIFY(dropOnSidebar(lists, model->index(model->rowForKey(QStringLiteral("id:last"))),
                          lists->visualRect(work).center()));
    QCOMPARE(model->rowForKey(QStringLiteral("id:last")), 4);
    QCOMPARE(model->rowForKey(QStringLiteral("id:work")), 5);
    QVERIFY(controller->getDocument()
                .groupForList(*controller->getDocument().findList(QStringLiteral("id:last")))
                ->m_isDefault);
    window->undo();
    QCOMPARE(window->getLatestContent().toUtf8(), source);

    work = model->index(model->rowForKey(QStringLiteral("id:work")));
    lists->scrollTo(work);
    QVERIFY(dropOnSidebar(lists, model->index(model->rowForKey(QStringLiteral("id:personal"))),
                          lists->visualRect(work).center()));
    QCOMPARE(model->rowForKey(QStringLiteral("id:personal")), 4);
    QCOMPARE(model->rowForKey(QStringLiteral("id:work")), 5);
    window->undo();
    QCOMPARE(window->getLatestContent().toUtf8(), source);
    controller->setGroupExpanded(QStringLiteral("id:projects"), false);
    const auto group = model->index(model->rowForGroup(QStringLiteral("id:projects")));
    lists->scrollTo(group);
    QVERIFY(dropOnSidebar(lists, model->index(model->rowForKey(QStringLiteral("id:work"))),
                          lists->visualRect(group).center()));
    QCOMPARE(controller->getDocument()
                 .groupForList(*controller->getDocument().findList(QStringLiteral("id:work")))
                 ->m_key,
             QStringLiteral("id:projects"));
    QVERIFY(!controller->getProjection().m_collapsedGroups.contains(QStringLiteral("id:projects")));
    window->undo();
    QCOMPARE(window->getLatestContent().toUtf8(), source);
  }
}

void TestTodoViewWindow::sidebarMenusAndDuplication_data() {
  QTest::addColumn<int>("width");
  QTest::newRow("wide") << 1000;
  QTest::newRow("narrow") << 600;
}

void TestTodoViewWindow::sidebarMenusAndDuplication() {
  QFETCH(int, width);
  createArea();
  QVERIFY(openNote(QStringLiteral("menus.todo")).isValid());
  auto *window = current();
  QVERIFY(window);
  m_area->resize(width, 760);
  auto *editor = window->getTodoEditor();
  auto *controller = window->getController();
  auto *model = controller->getListModel();
  auto *lists = control<QListView>(window, "todoLists");
  const auto openListMenu = [&](const QString &p_key) -> QMenu * {
    if (!showLists(window) || !activate(lists))
      return nullptr;
    const auto index = model->index(model->rowForKey(p_key));
    if (!index.isValid())
      return nullptr;
    lists->scrollTo(index);
    const auto point = lists->visualRect(index).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, lists->viewport()->mapToGlobal(point));
    QCoreApplication::sendEvent(lists->viewport(), &event);
    if (!QTest::qWaitFor([] { return QApplication::activePopupWidget(); }))
      return nullptr;
    return qobject_cast<QMenu *>(QApplication::activePopupWidget());
  };
  auto *menu = openListMenu(QStringLiteral("id:work"));
  QVERIFY(menu);
  QVERIFY(!control<QAction>(menu, "todoMoveListGroupAction")->isEnabled());
  menu->close();

  QString projectKey, secondKey;
  QVERIFY(controller->addGroup(QStringLiteral("Projects"), &projectKey));
  QVERIFY(controller->addGroup(QStringLiteral("Research & Notes"), &secondKey));
  menu = openListMenu(QStringLiteral("id:work"));
  QVERIFY(menu);
  auto *move = control<QAction>(menu, "todoMoveListGroupAction");
  QVERIFY(move && move->menu() && move->isEnabled());
  QStringList destinations;
  for (auto *action : move->menu()->actions())
    destinations.append(action->data().toString());
  QCOMPARE(destinations, QStringList({projectKey, secondKey}));
  QVERIFY(chooseOpenGroupDestination(projectKey));
  QCOMPARE(controller->getDocument()
               .groupForList(*controller->getDocument().findList(QStringLiteral("id:work")))
               ->m_key,
           projectKey);
  menu = openListMenu(QStringLiteral("id:work"));
  QVERIFY(menu);
  QVERIFY(chooseOpenMenu("todoRemoveListFromGroupAction"));
  QVERIFY(controller->getDocument()
              .groupForList(*controller->getDocument().findList(QStringLiteral("id:work")))
              ->m_isDefault);
  window->undo();
  QCOMPARE(controller->getDocument()
               .groupForList(*controller->getDocument().findList(QStringLiteral("id:work")))
               ->m_key,
           projectKey);

  QVERIFY(selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  controller->setGroupExpanded(projectKey, false);
  const auto beforeCopy = window->getLatestContent();
  auto *more = control<QPushButton>(window, "todoListMore");
  QVERIFY(chooseButtonMenu(more, "todoDuplicateListAction"));
  const auto copyKey = controller->getProjection().m_listKey;
  QVERIFY(copyKey != QStringLiteral("id:work"));
  const auto *copy = controller->getDocument().findList(copyKey);
  QVERIFY(copy);
  QCOMPARE(controller->getDocument().groupForList(*copy)->m_key, projectKey);
  QVERIFY(!controller->getProjection().m_collapsedGroups.contains(projectKey));
  QCOMPARE(copy->m_description, QStringLiteral("Team commitments"));
  QCOMPARE(copy->m_items.size(), 3);
  const auto copiedTask = copy->m_items.first().m_key;
  QVERIFY(copiedTask != QStringLiteral("id:alpha"));
  QVERIFY(openTask(window, copiedTask));
  QVERIFY(typeText(control<QLineEdit>(window, "todoTitle"), QStringLiteral("Independent copy")));
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields.m_title,
           QStringLiteral("Alpha"));
  window->undo();
  window->undo();
  QCOMPARE(window->getLatestContent(), beforeCopy);

  window->setMode(ViewWindowMode::Read);
  editor->focusGroup(projectKey);
  QVERIFY(openGroupMenu(window));
  menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
  for (auto *action : menu->actions())
    QVERIFY(!action->isEnabled());
  menu->close();
  controller->setGroupExpanded(projectKey, true);
  menu = openListMenu(QStringLiteral("id:work"));
  QVERIFY(menu);
  auto *sort = control<QAction>(menu, "todoSortByAction");
  QVERIFY(sort && sort->isEnabled());
  for (auto *action : menu->actions()) {
    if (action != sort)
      QVERIFY(!action->isEnabled());
  }
  QVERIFY(chooseOpenSort("todoSortImportanceAction"));
  QCOMPARE(controller->getProjection().m_sort, TodoSortOrder::Importance);
  QCOMPARE(taskTitles(window).first(), QStringLiteral("Beta"));
  QVERIFY(!controller->getTaskModel()->isReorderingEnabled());
  QCOMPARE(window->getLatestContent(), beforeCopy);
}

void TestTodoViewWindow::rootListsWideAndNarrow_data() {
  QTest::addColumn<int>("width");
  QTest::addColumn<bool>("hasRoots");
  QTest::newRow("wide-roots") << 1000 << true;
  QTest::newRow("narrow-roots") << 600 << true;
  QTest::newRow("wide-empty-default") << 1000 << false;
  QTest::newRow("narrow-empty-default") << 600 << false;
}

void TestTodoViewWindow::rootListsWideAndNarrow() {
  QFETCH(int, width);
  QFETCH(bool, hasRoots);
  QByteArray source =
      "# Projects\n<!-- todo-group: {\"id\":\"projects\"} -->\n\n"
      "## Grouped\n<!-- todo-list: {\"id\":\"grouped\"} -->\n\n"
      "- [ ] Custom task\n  <!-- todo-item: {\"id\":\"custom\"} -->\n\n"
      "# Default\n<!-- todo-group: {\"id\":\"named-default\"} -->\n\n"
      "## Named\n<!-- todo-list: {\"id\":\"named\"} -->\n\n"
      "- [ ] Literal task\n  <!-- todo-item: {\"id\":\"literal\"} -->\n\n"
      "# Private owner\n<!-- todo-group: {\"id\":\"default\",\"default\":true} -->\n\n";
  if (hasRoots) {
    source += "## Work\n<!-- todo-list: {\"id\":\"work\"} -->\n\n"
              "- [ ] Root task\n  <!-- todo-item: {\"id\":\"root\",\"important\":true} -->\n\n"
              "  > Root note\n\n"
              "## Personal\n<!-- todo-list: {\"id\":\"personal\"} -->\n\n"
              "- [ ] Home task\n  <!-- todo-item: {\"id\":\"home\"} -->\n";
  }
  createArea();
  QVERIFY(openNote(QStringLiteral("root-lists.todo"), source).isValid());
  auto *window = current();
  QVERIFY(window);
  m_area->resize(width, 760);
  auto *editor = window->getTodoEditor();
  QTRY_COMPARE(editor->width() < 840, width < 840);
  QVERIFY(showLists(window));
  auto *controller = window->getController();
  auto *model = controller->getListModel();
  auto *taskModel = controller->getTaskModel();
  auto *lists = control<QListView>(window, "todoLists");
  const auto defaultKey = QStringLiteral("id:default");
  const int rootCount = hasRoots ? 2 : 0;
  QCOMPARE(controller->getDocument().m_groups.last().m_key, defaultKey);
  QCOMPARE(model->rowForGroup(defaultKey), -1);
  QCOMPARE(model->rowCount(), 8 + rootCount);
  QCOMPARE(model->rowForGroup(QStringLiteral("id:projects")), 4 + rootCount);
  const auto namedDefault = model->index(model->rowForGroup(QStringLiteral("id:named-default")));
  QCOMPARE(namedDefault.row(), 6 + rootCount);
  QVERIFY(namedDefault.data(TodoListModel::GroupRole).toBool());
  QCOMPARE(namedDefault.data(TodoListModel::TitleRole).toString(), QStringLiteral("Default"));
  controller->setGroupExpanded(defaultKey, false);
  QVERIFY(controller->getProjection().m_collapsedGroups.contains(defaultKey));
  for (int row = 4; row < 4 + rootCount; ++row) {
    const auto index = model->index(row);
    QVERIFY(!index.data(TodoListModel::GroupRole).toBool());
    QVERIFY(index.data(TodoListModel::GroupIdRole).toString().isEmpty());
    QVERIFY(index.data(TodoListModel::GroupTitleRole).toString().isEmpty());
    QVERIFY(index.data(TodoListModel::ExpandedRole).toBool());
    QVERIFY(!lists->isRowHidden(row));
    QCOMPARE(model->groupAt(row)->m_key, defaultKey);
  }
  if (hasRoots) {
    QCOMPARE(model->rowForKey(QStringLiteral("id:work")), 4);
    QCOMPARE(model->rowForKey(QStringLiteral("id:personal")), 5);
  }

  QVERIFY(selectView(window, TodoViewKind::All));
  auto *destination = control<QComboBox>(window, "todoDestination");
  QVERIFY(destination);
  QCOMPARE(destination->itemText(destination->findData(QStringLiteral("id:grouped"))),
           QStringLiteral("Projects / Grouped"));
  QCOMPARE(destination->itemText(destination->findData(QStringLiteral("id:named"))),
           QStringLiteral("Default / Named"));
  if (hasRoots) {
    QCOMPARE(destination->itemText(destination->findData(QStringLiteral("id:work"))),
             QStringLiteral("Work"));
    QCOMPARE(destination->itemText(destination->findData(QStringLiteral("id:personal"))),
             QStringLiteral("Personal"));
  }
  int rootTasks = 0;
  for (int row = 0; row < taskModel->rowCount(); ++row) {
    const auto index = taskModel->index(row);
    if (!index.data(TodoTaskModel::SectionRole).toBool() &&
        index.data(TodoTaskModel::SourceGroupIdRole).toString() == defaultKey) {
      ++rootTasks;
      QVERIFY(index.data(TodoTaskModel::SourceGroupNameRole).toString().isEmpty());
    }
  }
  QCOMPARE(rootTasks, rootCount);
  controller->setSearch(QStringLiteral("Private owner"));
  QVERIFY(taskTitles(window).isEmpty());
  QCOMPARE(taskModel->getActiveCount(), 0);
  QCOMPARE(taskModel->getCompletedCount(), 0);
  for (int row = 0; row < model->rowCount(); ++row)
    QCOMPARE(model->index(row).data(TodoListModel::IncompleteCountRole).toInt(), 0);
  controller->setSearch(QStringLiteral("Default"));
  QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Literal task")}));
  if (hasRoots) {
    controller->setSearch(QStringLiteral("Work"));
    QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Root task")}));
    controller->setSearch(QStringLiteral("Root note"));
    QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Root task")}));
    controller->setSearch(QStringLiteral("Home task"));
    QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Home task")}));
  }
  controller->setSearch({});

  QVERIFY(showLists(window));
  auto *newList = control<QPushButton>(window, "todoNewList");
  QVERIFY(newList);
  QVERIFY(answerDialog(
      "todoNewListDialog",
      [&](QDialog *p_dialog) {
        auto *groups = control<QComboBox>(p_dialog, "todoNewListGroup");
        if (!groups || groups->count() != 3 || groups->itemText(0) != QLatin1String("No group") ||
            !groups->itemData(0).toString().isEmpty() ||
            !groups->currentData().toString().isEmpty() ||
            groups->itemData(1).toString() != QLatin1String("id:projects") ||
            groups->itemText(1) != QLatin1String("Projects") ||
            groups->itemData(2).toString() != QLatin1String("id:named-default") ||
            groups->itemText(2) != QLatin1String("Default"))
          return false;
        p_dialog->reject();
        return true;
      },
      [&] { newList->click(); }));

  controller->setView(TodoViewKind::Important);
  const auto before = controller->getProjection();
  const auto items = taskTitles(window);
  QSignalSpy taskReset(taskModel, &QAbstractItemModel::modelReset);
  editor->focusGroup(defaultKey);
  QCOMPARE(editor->selectedGroupKey(), defaultKey);
  QCOMPARE(lists->currentIndex().row(), hasRoots ? 4 : 1);
  controller->setGroupExpanded(QStringLiteral("id:projects"), false);
  QCOMPARE(lists->currentIndex().row(), hasRoots ? 4 : 1);
  QCOMPARE(editor->selectedGroupKey(), defaultKey);
  QCOMPARE(controller->getProjection().m_view, before.m_view);
  QCOMPARE(controller->getProjection().m_listKey, before.m_listKey);
  QCOMPARE(controller->getProjection().m_search, before.m_search);
  QCOMPARE(controller->getProjection().m_tag, before.m_tag);
  QCOMPARE(taskTitles(window), items);
  QCOMPARE(taskReset.size(), 0);
  window->setMode(ViewWindowMode::Read);
  QCOMPARE(lists->currentIndex().row(), hasRoots ? 4 : 1);
  for (int row = 4; row < 4 + rootCount; ++row)
    QVERIFY(!lists->isRowHidden(row));
  if (!hasRoots) {
    controller->setView(TodoViewKind::List, QStringLiteral("id:grouped"));
    editor->focusGroup(defaultKey);
    QCOMPARE(lists->currentIndex().data(TodoListModel::IdRole).toString(),
             QStringLiteral("id:grouped"));
    controller->setGroupExpanded(QStringLiteral("id:named-default"), false);
    QCOMPARE(lists->currentIndex().data(TodoListModel::IdRole).toString(),
             QStringLiteral("id:grouped"));
    QCOMPARE(editor->selectedGroupKey(), defaultKey);
    QCOMPARE(controller->getProjection().m_view, TodoViewKind::List);
    QCOMPARE(controller->getProjection().m_listKey, QStringLiteral("id:grouped"));
    QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Custom task")}));
  }
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  QVERIFY(!window->isModified());
}

void TestTodoViewWindow::keyboardGroupsSkipDefault_data() {
  QTest::addColumn<int>("width");
  QTest::addColumn<int>("defaultPosition");
  QTest::newRow("wide-before") << 1000 << 0;
  QTest::newRow("wide-between") << 1000 << 1;
  QTest::newRow("wide-after") << 1000 << 3;
  QTest::newRow("narrow-before") << 600 << 0;
  QTest::newRow("narrow-between") << 600 << 1;
  QTest::newRow("narrow-after") << 600 << 3;
}

void TestTodoViewWindow::keyboardGroupsSkipDefault() {
  QFETCH(int, width);
  QFETCH(int, defaultPosition);
  QList<QByteArray> blocks = {"# A\n<!-- todo-group: {\"id\":\"a\"} -->\n\n",
                              "# B\n<!-- todo-group: {\"id\":\"b\"} -->\n\n",
                              "# C\n<!-- todo-group: {\"id\":\"c\"} -->\n\n"};
  blocks.insert(defaultPosition, "# Internal owner\n<!-- todo-group: {\"default\":true} -->\n\n"
                                 "## Root\n\n- [ ] Keep\n\n");
  QByteArray source;
  for (const auto &block : blocks)
    source += block;
  createArea();
  QVERIFY(openNote(QStringLiteral("keyboard-groups.todo"), source).isValid());
  auto *window = current();
  QVERIFY(window);
  m_area->resize(width, 760);
  auto *editor = window->getTodoEditor();
  QTRY_COMPARE(editor->width() < 840, width < 840);
  auto *controller = window->getController();
  auto *model = controller->getListModel();
  auto *lists = control<QListView>(window, "todoLists");
  const auto groupOrder = [&] {
    QStringList keys;
    for (int row = 0; row < model->rowCount(); ++row) {
      const auto index = model->index(row);
      if (index.data(TodoListModel::GroupRole).toBool())
        keys.append(index.data(TodoListModel::GroupIdRole).toString());
    }
    return keys;
  };
  const QStringList originalOrder = {QStringLiteral("id:a"), QStringLiteral("id:b"),
                                     QStringLiteral("id:c")};
  QCOMPARE(groupOrder(), originalOrder);
  const auto undoSteps = window->getSourceEditor()->document()->availableUndoSteps();
  editor->focusGroup(QStringLiteral("id:a"));
  QVERIFY(activate(lists));
  QTest::keyClick(lists, Qt::Key_Up, Qt::AltModifier);
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  QCOMPARE(window->getSourceEditor()->document()->availableUndoSteps(), undoSteps);
  QVERIFY(!window->isModified());
  editor->focusGroup(QStringLiteral("id:c"));
  QVERIFY(activate(lists));
  QTest::keyClick(lists, Qt::Key_Down, Qt::AltModifier);
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  QCOMPARE(window->getSourceEditor()->document()->availableUndoSteps(), undoSteps);
  QVERIFY(!window->isModified());

  struct Move {
    const char *key;
    Qt::Key direction;
    QStringList order;
  };
  const Move moves[] = {
      {"id:a", Qt::Key_Down, {"id:b", "id:a", "id:c"}},
      {"id:b", Qt::Key_Up, {"id:b", "id:a", "id:c"}},
      {"id:b", Qt::Key_Down, {"id:a", "id:c", "id:b"}},
      {"id:c", Qt::Key_Up, {"id:a", "id:c", "id:b"}},
  };
  for (const auto &move : moves) {
    editor->focusGroup(QString::fromLatin1(move.key));
    QVERIFY(activate(lists));
    QTest::keyClick(lists, move.direction, Qt::AltModifier);
    QCOMPARE(groupOrder(), move.order);
    QCOMPARE(editor->selectedGroupKey(), QString::fromLatin1(move.key));
    QCOMPARE(model->rowForGroup(controller->getDocument().defaultGroup()->m_key), -1);
    QCOMPARE(model->index(4).data(TodoListModel::TitleRole).toString(), QStringLiteral("Root"));
    const auto moved = window->getLatestContent();
    QVERIFY(moved.toUtf8() != source);
    window->undo();
    QCOMPARE(window->getLatestContent().toUtf8(), source);
    QCOMPARE(groupOrder(), originalOrder);
    window->redo();
    QCOMPARE(window->getLatestContent(), moved);
    QCOMPARE(groupOrder(), move.order);
    window->undo();
    QCOMPARE(window->getLatestContent().toUtf8(), source);
  }
}

void TestTodoViewWindow::nativeGroupsWideAndNarrow_data() {
  QTest::addColumn<int>("width");
  QTest::newRow("wide") << 1000;
  QTest::newRow("narrow") << 600;
}

void TestTodoViewWindow::nativeGroupsWideAndNarrow() {
  QFETCH(int, width);
  createArea();
  auto source = c_source;
  source.replace("# Default\n<!-- todo-group: {\"id\":\"default\",\"default\":true} -->\n\n",
                 "# Internal\n<!-- todo-group: {\"id\":\"default\",\"default\":true} -->\n\n"
                 "# Team\n<!-- todo-group: {\"id\":\"team\"} -->\n\n");
  const auto buffer = openNote(QStringLiteral("groups.todo"), source);
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window);
  m_area->resize(width, 760);
  auto *editor = window->getTodoEditor();
  auto *controller = window->getController();
  auto *model = controller->getListModel();
  auto *taskModel = controller->getTaskModel();
  auto *lists = control<QListView>(window, "todoLists");
  auto *newGroup = control<QPushButton>(window, "todoNewGroup");
  auto *newList = control<QPushButton>(window, "todoNewList");
  QVERIFY(lists && newGroup && newList);
  QTRY_COMPARE(editor->width() < 840, width < 840);
  QVERIFY(showLists(window));
  QTRY_VERIFY(newGroup->isVisible() && newList->isVisible());
  const auto defaultKey = QStringLiteral("id:default");
  const auto teamKey = QStringLiteral("id:team");
  const int teamRow = model->rowForGroup(teamKey);
  const int workRow = model->rowForKey(QStringLiteral("id:work"));
  QVERIFY(teamRow >= 0 && workRow >= 0);
  QVERIFY(model->groupAt(teamRow));
  QCOMPARE(model->groupAt(workRow)->m_key, teamKey);
  QCOMPARE(model->rowForGroup(defaultKey), -1);
  QVERIFY(!model->groupAt(0));
  QVERIFY(model->index(teamRow).data(TodoListModel::GroupRole).toBool());
  QVERIFY(!model->index(teamRow).data(TodoListModel::ViewKindRole).isValid());
  QCOMPARE(model->index(teamRow).data(TodoListModel::ListCountRole).toInt(), 2);
  QCOMPARE(model->index(teamRow).data(TodoListModel::IncompleteCountRole).toInt(), 3);
  const auto original = window->getLatestContent();
  const auto originalView = controller->getProjection().m_view;
  const auto originalList = controller->getProjection().m_listKey;
  const auto originalItems = taskTitles(window);
  const auto originalSelection = controller->getSelectedItemKey();
  editor->focusGroup(teamKey);
  QSignalSpy taskReset(taskModel, &QAbstractItemModel::modelReset);
  if (width >= 840) {
    QVERIFY(activate(lists));
    const auto index = model->index(teamRow);
    lists->scrollTo(index);
    QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
    const auto expansionChanges = [&]() {
      int count = 0;
      for (const auto &change : changes) {
        if (change.at(2).value<QList<int>>().contains(TodoListModel::ExpandedRole))
          ++count;
      }
      return count;
    };
    const auto center = lists->visualRect(index).center();
    QTest::mouseClick(lists->viewport(), Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(editor->selectedGroupKey(), teamKey);
    QVERIFY(!controller->getProjection().m_collapsedGroups.contains(teamKey));
    QVERIFY(!lists->isRowHidden(workRow));
    const auto chevron = QPoint(lists->visualRect(index).right() - 22, center.y());
    QTest::mouseClick(lists->viewport(), Qt::LeftButton, Qt::NoModifier, chevron);
    QCOMPARE(expansionChanges(), 0);
    QVERIFY(!lists->isRowHidden(workRow));
    // Include the first click and the final release, not just MouseButtonDblClick.
    const auto doubleClick = [&]() {
      QTest::mouseClick(lists->viewport(), Qt::LeftButton, Qt::NoModifier, center);
      QTest::mouseDClick(lists->viewport(), Qt::LeftButton, Qt::NoModifier, center);
      QTest::mouseRelease(lists->viewport(), Qt::LeftButton, Qt::NoModifier, center);
    };
    doubleClick();
    QCOMPARE(expansionChanges(), 1);
    QVERIFY(lists->isRowHidden(workRow));
    doubleClick();
    QCOMPARE(expansionChanges(), 2);
    QVERIFY(!lists->isRowHidden(workRow));
    doubleClick();
    QCOMPARE(expansionChanges(), 3);
  } else {
    QVERIFY(toggleGroupInSidebar(window));
  }
  QVERIFY(controller->getProjection().m_collapsedGroups.contains(teamKey));
  QVERIFY(lists->isRowHidden(workRow));
  QCOMPARE(model->rowForKey(QStringLiteral("id:work")), workRow);
  QCOMPARE(taskReset.size(), 0);
  QCOMPARE(taskTitles(window), originalItems);
  QCOMPARE(controller->getSelectedItemKey(), originalSelection);
  QCOMPARE(controller->getProjection().m_view, originalView);
  QCOMPARE(controller->getProjection().m_listKey, originalList);
  QCOMPARE(window->getLatestContent(), original);
  QVERIFY(!window->isModified());
  auto *search = control<QLineEdit>(window, "todoSearch");
  QVERIFY(typeText(search, QStringLiteral("Team")));
  QCOMPARE(taskModel->getActiveCount(), 3);
  QCOMPARE(taskModel->getCompletedCount(), 1);
  for (int row = 0; row < taskModel->rowCount(); ++row) {
    const auto index = taskModel->index(row);
    if (!index.data(TodoTaskModel::SectionRole).toBool()) {
      QCOMPARE(index.data(TodoTaskModel::SourceGroupIdRole).toString(), teamKey);
      QCOMPARE(index.data(TodoTaskModel::SourceGroupNameRole).toString(), QStringLiteral("Team"));
    }
  }
  search->clear();
  QVERIFY(selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  QVERIFY(!controller->getProjection().m_collapsedGroups.contains(teamKey));
  QVERIFY(!lists->isRowHidden(workRow));
  QCOMPARE(window->getLatestContent(), original);
  if (width >= 840) {
    QVERIFY(activate(lists));
    const auto index = model->index(workRow);
    lists->scrollTo(index);
    const auto point = lists->visualRect(index).center();
    QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
    QTest::mouseClick(lists->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseDClick(lists->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseRelease(lists->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    for (const auto &change : changes)
      QVERIFY(!change.at(2).value<QList<int>>().contains(TodoListModel::ExpandedRole));
    QVERIFY(!controller->getProjection().m_collapsedGroups.contains(teamKey));
    QVERIFY(!lists->isRowHidden(workRow));
    QCOMPARE(controller->getProjection().m_listKey, QStringLiteral("id:work"));
    QCOMPARE(window->getLatestContent(), original);
  }

  const auto createGroup = [&](const QString &p_title) {
    if (!showLists(window))
      return false;
    return answerDialog(
        "todoNewGroupDialog",
        [&](QDialog *p_dialog) {
          auto *name = control<QLineEdit>(p_dialog, "todoNewGroupName");
          if (!name)
            return false;
          name->setText(p_title);
          p_dialog->accept();
          return true;
        },
        [&] { newGroup->click(); });
  };
  QVERIFY(createGroup(QStringLiteral("Projects")));
  const auto projectKey = editor->selectedGroupKey();
  QVERIFY(!projectKey.isEmpty() && projectKey != defaultKey);
  QCOMPARE(controller->getDocument().findGroup(projectKey)->m_title, QStringLiteral("Projects"));
  QCOMPARE(controller->getDocument().findGroup(projectKey)->m_listCount, 0);
  QCOMPARE(controller->getProjection().m_listKey, QStringLiteral("id:work"));
  QCOMPARE(taskTitles(window), originalItems);
  QVERIFY(toggleGroupInSidebar(window));
  QVERIFY(controller->getProjection().m_collapsedGroups.contains(projectKey));
  QVERIFY(toggleGroupInSidebar(window));
  QVERIFY(!controller->getProjection().m_collapsedGroups.contains(projectKey));
  QVERIFY(createGroup(QStringLiteral("Scratch")));
  const auto scratchKey = editor->selectedGroupKey();
  const auto withScratch = window->getLatestContent();
  QVERIFY(chooseGroupMenu(window, "todoUngroupListsAction"));
  QVERIFY(!QApplication::activeModalWidget());
  QVERIFY(!controller->getDocument().findGroup(scratchKey));
  window->undo();
  QCOMPARE(window->getLatestContent(), withScratch);
  window->redo();
  QVERIFY(!controller->getDocument().findGroup(scratchKey));
  QVERIFY(createGroup(QStringLiteral("Archive")));
  const auto archiveKey = editor->selectedGroupKey();
  QVERIFY(archiveKey != projectKey);
  editor->focusGroup(archiveKey);
  QWidget *groupFocus = lists;
  QVERIFY(activate(groupFocus));
  QTest::keyClick(groupFocus, Qt::Key_Up, Qt::AltModifier);
  QCOMPARE(controller->getDocument().m_groups.at(2).m_key, archiveKey);
  QTest::keyClick(groupFocus, Qt::Key_Down, Qt::AltModifier);
  QCOMPARE(controller->getDocument().m_groups.last().m_key, archiveKey);
  bool renamedAction = false;
  QVERIFY(answerDialog(
      "todoRenameGroupDialog",
      [&](QDialog *p_dialog) {
        auto *name = control<QLineEdit>(p_dialog, "todoRenameGroupName");
        if (!name || name->text() != QLatin1String("Archive"))
          return false;
        name->setText(QStringLiteral("Reference"));
        p_dialog->accept();
        return true;
      },
      [&] { renamedAction = chooseGroupMenu(window, "todoRenameGroupAction"); }));
  QVERIFY(renamedAction);
  QCOMPARE(controller->getDocument().findGroup(archiveKey)->m_title, QStringLiteral("Reference"));
  bool listAction = false;
  QVERIFY(answerDialog(
      "todoNewListDialog",
      [&](QDialog *p_dialog) {
        auto *name = control<QLineEdit>(p_dialog, "todoNewListName");
        auto *group = control<QComboBox>(p_dialog, "todoNewListGroup");
        auto *accept = control<QPushButton>(p_dialog, "todoNewListAccept");
        if (!name || !group || !accept || group->currentData().toString() != archiveKey)
          return false;
        name->setText(QStringLiteral("Work"));
        accept->click();
        return true;
      },
      [&] { listAction = chooseGroupMenu(window, "todoGroupNewListAction"); }));
  QVERIFY(listAction);
  const auto referenceList = controller->getProjection().m_listKey;
  QVERIFY(referenceList != QLatin1String("id:work"));
  QCOMPARE(controller->getDocument()
               .groupForList(*controller->getDocument().findList(referenceList))
               ->m_key,
           archiveKey);
  QVERIFY(quickAdd(window, QStringLiteral("Reference task")));
  const auto referenceTask = controller->getSelectedItemKey();
  QVERIFY(showLists(window));
  QVERIFY(answerNewList(QStringLiteral("Inbox"), [&] { newList->click(); }));
  const auto inbox = controller->getProjection().m_listKey;
  QCOMPARE(
      controller->getDocument().groupForList(*controller->getDocument().findList(inbox))->m_key,
      defaultKey);
  QVERIFY(controller->getDocument().m_lists.last().m_key != inbox);
  QCOMPARE(editor->selectedGroupKey(), defaultKey);

  QVERIFY(selectView(window, TodoViewKind::List, QStringLiteral("id:work")));
  const auto alphaBefore = *controller->getDocument().findItem(QStringLiteral("id:alpha"));
  auto *listMore = control<QPushButton>(window, "todoListMore");
  QVERIFY(listMore && listMore->isVisible());
  QVERIFY(chooseButtonMenu(listMore, "todoRemoveListFromGroupAction"));
  QCOMPARE(controller->getDocument()
               .groupForList(*controller->getDocument().findList(QStringLiteral("id:work")))
               ->m_key,
           defaultKey);
  const auto moveTo = [&](const QString &p_groupKey) {
    if (!activate(listMore))
      return false;
    QTest::mouseClick(listMore, Qt::LeftButton);
    return chooseOpenGroupDestination(p_groupKey);
  };
  QVERIFY(moveTo(projectKey));
  QCOMPARE(controller->getDocument()
               .groupForList(*controller->getDocument().findList(QStringLiteral("id:work")))
               ->m_key,
           projectKey);
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields.m_note,
           alphaBefore.m_fields.m_note);
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_steps.size(),
           alphaBefore.m_steps.size());
  const auto moved = window->getLatestContent();
  QVERIFY(showLists(window));
  QVERIFY(activate(lists));
  QTest::keyClick(lists, Qt::Key_Up, Qt::AltModifier);
  QCOMPARE(window->getLatestContent(), moved);
  QTest::keyClick(lists, Qt::Key_Down, Qt::AltModifier);
  QCOMPARE(window->getLatestContent(), moved); // The only list cannot cross its group boundary.
  window->undo();
  QCOMPARE(controller->getDocument()
               .groupForList(*controller->getDocument().findList(QStringLiteral("id:work")))
               ->m_key,
           defaultKey);
  window->redo();
  QCOMPARE(window->getLatestContent(), moved);
  QVERIFY(selectView(window, TodoViewKind::All));
  auto *destination = control<QComboBox>(window, "todoDestination");
  QVERIFY(destination);
  QCOMPARE(destination->itemText(destination->findData(QStringLiteral("id:work"))),
           QStringLiteral("Projects / Work"));
  QCOMPARE(destination->itemText(destination->findData(referenceList)),
           QStringLiteral("Reference / Work"));

  editor->focusGroup(projectKey);
  QVERIFY(chooseGroupMenu(window, "todoUngroupListsAction"));
  QVERIFY(!QApplication::activeModalWidget());
  QVERIFY(!controller->getDocument().findGroup(projectKey));
  const auto *ungrouped = controller->getDocument().findList(QStringLiteral("id:work"));
  QVERIFY(ungrouped);
  QCOMPARE(controller->getDocument().groupForList(*ungrouped)->m_key, defaultKey);
  QVERIFY(controller->getDocument().findItem(QStringLiteral("id:alpha")));
  QVERIFY(controller->getDocument().findItem(referenceTask));
  QVERIFY(window->getLatestContent().contains(QStringLiteral("File-scoped author comment")));
  window->undo();
  QCOMPARE(window->getLatestContent(), moved);
  window->redo();
  const auto saved = window->getLatestContent();
  QVERIFY(saved.contains(QStringLiteral("\"version\":1")));
  QVERIFY(saved.contains(QStringLiteral("# Reference\n")));
  QVERIFY(saved.contains(QStringLiteral("## Work\n")));
  QVERIFY(window->getSourceEditor()->isHidden());
  QVERIFY(window->save());
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()));
  QCOMPARE(readBytes(absolute(QStringLiteral("groups.todo"))), saved.toUtf8());

  window->setMode(ViewWindowMode::Read);
  QVERIFY(!newGroup->isEnabled() && !newList->isEnabled());
  QVERIFY(showLists(window));
  const auto archiveIndex = model->index(model->rowForGroup(archiveKey));
  QVERIFY(activate(lists));
  lists->scrollTo(archiveIndex);
  QTest::mouseClick(lists->viewport(), Qt::LeftButton, Qt::NoModifier,
                    lists->visualRect(archiveIndex).center());
  QCOMPARE(editor->selectedGroupKey(), archiveKey);
  QVERIFY(openGroupMenu(window));
  auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
  for (auto *action : menu->actions()) {
    if (action->isSeparator())
      continue;
    QVERIFY(!action->isEnabled());
  }
  menu->close();
  QVERIFY(toggleGroupInSidebar(window));
  QVERIFY(controller->getProjection().m_collapsedGroups.contains(archiveKey));
  editor->focusGroup(archiveKey);
  QVERIFY(activate(groupFocus));
  QTest::keyClick(groupFocus, Qt::Key_Up, Qt::AltModifier);
  QCOMPARE(window->getLatestContent(), saved);
  QVERIFY(!window->isModified());
  QVERIFY(!controller->ungroupLists(archiveKey));
  const auto node = buffer.nodeId();
  QVERIFY(m_area->getController()->closeViewWindow(window->getViewWindowId(), false));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(m_buffers->openBuffer(node).isValid());
  window = current();
  QVERIFY(window);
  QCOMPARE(window->getLatestContent(), saved);
  QCOMPARE(window->getController()->getDocument().findGroup(archiveKey)->m_title,
           QStringLiteral("Reference"));
  QVERIFY(window->getController()->getDocument().findItem(referenceTask));
  QVERIFY(window->getController()->getProjection().m_collapsedGroups.isEmpty());
  QVERIFY(!window->isModified());
}

void TestTodoViewWindow::implicitDefaultAndGroupRecreation() {
  createArea();
  const QByteArray source =
      "<!-- file-owned -->\n\n## Inbox\n\n- [ ] Existing\n\n# Other\n\n## Elsewhere\n";
  QVERIFY(openNote(QStringLiteral("implicit.todo"), source).isValid());
  auto *window = current();
  QVERIFY(window);
  auto *controller = window->getController();
  auto *editor = window->getTodoEditor();
  auto *newList = control<QPushButton>(window, "todoNewList");
  QVERIFY(newList);
  QVERIFY(controller->getDocument().defaultGroup());
  QCOMPARE(controller->getDocument().defaultGroup()->m_key, QStringLiteral("default:"));
  editor->focusGroup(QStringLiteral("default:"));
  auto *model = controller->getListModel();
  auto *lists = control<QListView>(window, "todoLists");
  QCOMPARE(model->rowForGroup(QStringLiteral("default:")), -1);
  QCOMPARE(lists->currentIndex().row(), 4);
  QVERIFY(!lists->isRowHidden(4));
  QVERIFY(lists->currentIndex().data(TodoListModel::GroupIdRole).toString().isEmpty());
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  QVERIFY(!window->isModified());
  QVERIFY(window->save());
  QCOMPARE(readBytes(absolute(QStringLiteral("implicit.todo"))), source);
  QVERIFY(answerNewList(QStringLiteral("New default list"), [&] { newList->click(); }));
  const auto createdKey = controller->getProjection().m_listKey;
  const auto defaultKey = controller->getDocument().defaultGroup()->m_key;
  QVERIFY(defaultKey.startsWith(QLatin1String("id:")));
  QVERIFY(!controller->getDocument().defaultGroup()->m_implicit);
  QCOMPARE(controller->getDocument().defaultGroup()->m_listCount, 2);
  QCOMPARE(controller->getDocument()
               .groupForList(*controller->getDocument().findList(createdKey))
               ->m_key,
           defaultKey);
  QVERIFY(controller->getDocument().m_lists.last().m_key != createdKey);
  const auto materialized = window->getLatestContent();
  QVERIFY(materialized.indexOf(QStringLiteral("# Default")) >
          materialized.indexOf(QStringLiteral("file-owned")));
  QVERIFY(materialized.indexOf(QStringLiteral("# Default")) <
          materialized.indexOf(QStringLiteral("## Inbox")));
  window->undo();
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  window->redo();
  QCOMPARE(window->getLatestContent(), materialized);
  QCOMPARE(model->rowForGroup(defaultKey), -1);
  QCOMPARE(model->rowForKey(createdKey), 5);
  QVERIFY(selectView(window, TodoViewKind::List, createdKey));
  QVERIFY(
      chooseButtonMenu(control<QPushButton>(window, "todoListMore"), "todoDuplicateListAction"));
  const auto copyKey = controller->getProjection().m_listKey;
  QVERIFY(copyKey != createdKey);
  QVERIFY(controller->getDocument()
              .groupForList(*controller->getDocument().findList(copyKey))
              ->m_isDefault);
  QCOMPARE(model->rowForGroup(defaultKey), -1);
  const auto copyIndex = model->index(model->rowForKey(copyKey));
  QVERIFY(copyIndex.data(TodoListModel::GroupIdRole).toString().isEmpty());
  QVERIFY(!copyIndex.data(TodoListModel::GroupRole).toBool());
  window->undo();
  QCOMPARE(window->getLatestContent(), materialized);
  const auto otherKey = controller->getDocument().m_groups.last().m_key;
  editor->focusGroup(otherKey);
  QVERIFY(chooseGroupMenu(window, "todoUngroupListsAction"));
  QVERIFY(!controller->getDocument().findGroup(otherKey));
  QCOMPARE(controller->getDocument().defaultGroup()->m_key, defaultKey);
  QCOMPARE(controller->getDocument().defaultGroup()->m_listCount, 3);
  QVERIFY(window->getLatestContent().contains(QStringLiteral("file-owned")));
  QVERIFY(window->getLatestContent().contains(QStringLiteral("## Elsewhere")));
  QCOMPARE(model->rowForGroup(defaultKey), -1);
  QCOMPARE(model->rowCount(), 7);
  for (int row = 4; row < model->rowCount(); ++row) {
    QVERIFY(!model->index(row).data(TodoListModel::GroupRole).toBool());
    QVERIFY(model->index(row).data(TodoListModel::GroupIdRole).toString().isEmpty());
    QVERIFY(!lists->isRowHidden(row));
  }
  // Ungrouping an empty group removes only its container and is undoable.
  QVERIFY(controller->addGroup(QStringLiteral("Empty")));
  const auto emptyKey = controller->getDocument().m_groups.last().m_key;
  editor->focusGroup(emptyKey);
  QVERIFY(chooseGroupMenu(window, "todoUngroupListsAction"));
  QVERIFY(!QApplication::activeModalWidget());
  QVERIFY(!controller->getDocument().findGroup(emptyKey));
  window->undo();
  QVERIFY(controller->getDocument().findGroup(emptyKey));
}

void TestTodoViewWindow::groupDialogsRejectForeignReload() {
  createArea();
  const auto source = c_source + "\n# Projects\n<!-- todo-group: {\"id\":\"projects\"} -->\n";
  QVERIFY(openNote(QStringLiteral("group-modal.todo"), source).isValid());
  auto *window = current();
  QVERIFY(window);
  auto *controller = window->getController();
  auto replacement = source;
  replacement.replace("# Projects", "# Reloaded");
  bool chosen = false;
  QVERIFY(answerDialog(
      "todoNewListDialog",
      [&](QDialog *p_dialog) {
        auto *name = control<QLineEdit>(p_dialog, "todoNewListName");
        auto *accept = control<QPushButton>(p_dialog, "todoNewListAccept");
        if (!name || !accept ||
            !writeBytes(absolute(QStringLiteral("group-modal.todo")), replacement) ||
            !window->reload())
          return false;
        name->setText(QStringLiteral("Stale list"));
        accept->click();
        return true;
      },
      [&] {
        window->getTodoEditor()->focusGroup(QStringLiteral("id:projects"));
        chosen = chooseGroupMenu(window, "todoGroupNewListAction");
      }));
  QVERIFY(chosen);
  QCOMPARE(window->getLatestContent().toUtf8(), replacement);
  QCOMPARE(controller->getDocument().m_lists.size(), 2);
  QVERIFY(!window->isModified());
  QVERIFY(openGroupMenu(window));
  auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
  QPointer<QAction> staleRename = control<QAction>(menu, "todoRenameGroupAction");
  QVERIFY(staleRename);
  QVERIFY(writeBytes(absolute(QStringLiteral("group-modal.todo")), source));
  QVERIFY(window->reload());
  if (staleRename)
    staleRename->trigger();
  QVERIFY(!QApplication::activeModalWidget());
  if (auto *popup = QApplication::activePopupWidget())
    popup->close();
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  QVERIFY(controller->getDocument().findGroup(QStringLiteral("id:projects")));
  QVERIFY(controller->getDocument().findItem(QStringLiteral("id:alpha")));
  QVERIFY(!window->isModified());
}

void TestTodoViewWindow::deletionConfirmationRejectsForeignReload() {
  createArea();
  auto store = std::make_shared<Store>();
  QVERIFY(m_buffers->openMemoryBuffer(storedSource(store)).isValid());
  auto *window = current();
  QVERIFY(window && openTask(window, QStringLiteral("id:alpha")));
  auto authoritative = c_source;
  authoritative.replace("Alpha", "Changed while confirmation was open");
  bool reloaded = false;
  DialogAnswers dialog({QMessageBox::Yes}, [&](QMessageBox *) {
    {
      QMutexLocker lock(&store->mutex);
      store->bytes = authoritative;
    }
    reloaded = window->reload();
  });
  QVERIFY(chooseTaskMenu(window, QStringLiteral("id:alpha"), "todoDeleteTaskAction"));
  QVERIFY(dialog.accepted());
  QVERIFY(reloaded);
  QCOMPARE(window->getLatestContent().toUtf8(), authoritative);
  const auto *item = window->getController()->getDocument().findItem(QStringLiteral("id:alpha"));
  QVERIFY(item);
  QCOMPARE(item->m_fields.m_title, QStringLiteral("Changed while confirmation was open"));
  QVERIFY(!window->isModified());
}

void TestTodoViewWindow::invalidSourceRecovery_data() {
  QTest::addColumn<QByteArray>("broken");
  auto malformed = c_source;
  malformed.replace("\"id\":\"alpha\"", "\"id\":");
  QTest::newRow("malformed-owned-json") << malformed;
  QTest::newRow("unquoted-root-prose") << c_source + "\nUnsupported loose prose\n";
  QTest::newRow("unaccounted-reference-definition")
      << c_source + "\n[hidden]: https://example.com\n";
}

void TestTodoViewWindow::invalidSourceRecovery() {
  QFETCH(QByteArray, broken);
  createArea();
  QVERIFY(openNote(QStringLiteral("broken.todo"), broken).isValid());
  auto *window = current();
  QVERIFY(window);
  auto *banner = control<InlineBanner>(window, "todoDiagnosticBanner");
  auto *controller = window->getController();
  QVERIFY(banner);
  QTRY_VERIFY(window->getTodoEditor()->isVisible());
  QVERIFY(window->getSourceEditor()->isHidden());
  QTRY_VERIFY(banner->isVisible());
  QVERIFY(!banner->getText().isEmpty());
  QVERIFY(!controller->getDocument().isValid());
  const int line = controller->getDocument().m_diagnostics.first().m_line;
  QVERIFY(banner->getText().contains(QString::number(line)));
  QCOMPARE(window->getLatestContent().toUtf8(), broken);
  QVERIFY(!controller->addList(QStringLiteral("Must not erase malformed source")));
  QCOMPARE(window->getLatestContent().toUtf8(), broken);
  QVERIFY(window->save());
  QCOMPARE(readBytes(absolute(QStringLiteral("broken.todo"))), broken);
  QVERIFY(writeBytes(absolute(QStringLiteral("broken.todo")), c_source));
  QVERIFY(window->reload());
  QVERIFY(controller->getDocument().isValid());
  QVERIFY(!banner->isVisible());
  QVERIFY(window->getSourceEditor()->isHidden());
  QCOMPARE(window->getLatestContent().toUtf8(), c_source);
  QVERIFY(quickAdd(window, QStringLiteral("Recovered task")));
  window->undo();
  QCOMPARE(window->getLatestContent().toUtf8(), c_source);
  QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
  QVERIFY(window->save());
}

void TestTodoViewWindow::repeatedSaveAutosaveAndReopen_data() {
  QTest::addColumn<bool>("crlf");
  QTest::addColumn<bool>("eofNewline");
  QTest::newRow("lf-eof-newline") << false << true;
  QTest::newRow("lf-no-final-newline") << false << false;
  QTest::newRow("crlf-eof-newline") << true << true;
  QTest::newRow("crlf-no-final-newline") << true << false;
}

void TestTodoViewWindow::repeatedSaveAutosaveAndReopen() {
  QFETCH(bool, crlf);
  QFETCH(bool, eofNewline);
  createArea();
  QString initial =
      QString::fromUtf8("<!-- untouched α 😀 -->\n# Default\n"
                        "<!-- todo-group: {\"id\":\"default\",\"default\":true} -->\n\n"
                        "## 工作\n"
                        "<!-- todo-list: {\"id\":\"work\"} -->\n\n"
                        "- [ ] Café 😀\n  <!-- todo-item: {\"id\":\"alpha\"} -->");
  if (eofNewline)
    initial += QLatin1Char('\n');
  if (crlf)
    initial.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
  const auto buffer = openNote(QStringLiteral("unicode.todo"), initial.toUtf8());
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window);
  focusTasks(window);
  QCOMPARE(window->getLatestContent(), initial);
  QVERIFY(!window->getSourceEditor()->toPlainText().contains(QLatin1Char('\r')));
  auto *controller = window->getController();
  auto fields = controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields;
  fields.m_checked = true;
  QVERIFY(controller->updateTask(QStringLiteral("id:alpha"), fields));
  const auto ending = crlf ? QStringLiteral("\r\n") : QStringLiteral("\n");
  QString expected = QStringLiteral("<!-- todo: {\"version\":1} -->") + ending + ending + initial;
  expected.replace(QStringLiteral("- [ ]"), QStringLiteral("- [x]"));
  QCOMPARE(window->getLatestContent(), expected);
  QVERIFY(window->save());
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()));
  QCOMPARE(readBytes(absolute(QStringLiteral("unicode.todo"))), expected.toUtf8());
  QVERIFY(!window->isModified());
  QCOMPARE(window->getLatestContent(), expected);
  QVERIFY(window->save());
  QCOMPARE(readBytes(absolute(QStringLiteral("unicode.todo"))), expected.toUtf8());
  fields.m_title = QString::fromUtf8("Révisé 🗓");
  QVERIFY(controller->updateTask(QStringLiteral("id:alpha"), fields));
  expected.replace(QString::fromUtf8("Café 😀"), fields.m_title);
  QCOMPARE(window->getLatestContent(), expected);
  auto *timer = m_buffers->asQObject()->findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
  QVERIFY(timer);
  timer->setInterval(60 * 60 * 1000);
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()) && !window->isModified());
  QCOMPARE(readBytes(absolute(QStringLiteral("unicode.todo"))), expected.toUtf8());
  QCOMPARE(window->getLatestContent(), expected);
  fields.m_checked = false;
  QVERIFY(controller->updateTask(QStringLiteral("id:alpha"), fields));
  expected.replace(QStringLiteral("- [x]"), QStringLiteral("- [ ]"));
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()) && !window->isModified());
  QCOMPARE(window->getLatestContent(), expected);
  QCOMPARE(readBytes(absolute(QStringLiteral("unicode.todo"))), expected.toUtf8());
  QCOMPARE(expected.endsWith(QLatin1Char('\n')), eofNewline);
  m_buffers->setAutoSavePolicy(AutoSavePolicy::None);
  const auto node = buffer.nodeId();
  const auto id = window->getViewWindowId();
  QPointer<TodoViewWindow> retired(window);
  QVERIFY(m_area->getController()->closeViewWindow(id, false));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(retired.isNull());
  QVERIFY(m_buffers->openBuffer(node).isValid());
  window = current();
  QVERIFY(window);
  QCOMPARE(window->getLatestContent(), expected);
  QVERIFY(!window->isModified());
  QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
  QCOMPARE(
      window->getController()->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields.m_title,
      fields.m_title);
}

void TestTodoViewWindow::closeDecisions_data() {
  QTest::addColumn<int>("answer");
  QTest::newRow("cancel-keeps-window") << int(QMessageBox::Cancel);
  QTest::newRow("discard-keeps-disk") << int(QMessageBox::Discard);
  QTest::newRow("save-persists-and-closes") << int(QMessageBox::Save);
}

void TestTodoViewWindow::closeDecisions() {
  QFETCH(int, answer);
  createArea();
  const auto buffer = openNote(QStringLiteral("close.todo"));
  QVERIFY(buffer.isValid());
  QPointer<TodoViewWindow> window(current());
  QVERIFY(window);
  focusTasks(window);
  QVERIFY(quickAdd(window, QStringLiteral("Unsaved close task")));
  const auto edited = window->getLatestContent();
  bool named = false;
  DialogAnswers dialog({static_cast<QMessageBox::StandardButton>(answer)}, [&](QMessageBox *box) {
    named = box->text().contains(QStringLiteral("close.todo"));
  });
  const bool closed = m_area->getController()->closeViewWindow(window->getViewWindowId(), false);
  QVERIFY(dialog.accepted());
  QVERIFY(named);
  QCOMPARE(closed, answer != int(QMessageBox::Cancel));
  if (!closed) {
    QVERIFY(window && window->isModified());
    QCOMPARE(window->getLatestContent(), edited);
  } else {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(window.isNull());
  }
  QCOMPARE(readBytes(absolute(QStringLiteral("close.todo"))),
           answer == int(QMessageBox::Save) ? edited.toUtf8() : c_source);
}

void TestTodoViewWindow::saveAndReloadFailuresRetainDraft() {
  createArea();
  auto store = std::make_shared<Store>();
  const auto buffer = m_buffers->openMemoryBuffer(storedSource(store));
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window);
  QVERIFY(buffer.canSave() && buffer.canReload());
  QVERIFY(buffer.resolvedPath().isEmpty());
  focusTasks(window);
  QVERIFY(quickAdd(window, QStringLiteral("Keep this draft")));
  const auto draft = window->getLatestContent();
  {
    QMutexLocker lock(&store->mutex);
    store->saveError = VXCORE_ERR_IO;
    store->loadError = VXCORE_ERR_IO;
  }
  QVERIFY(!window->save());
  QVERIFY(window->isModified());
  QCOMPARE(window->getLatestContent(), draft);
  QCOMPARE(store->snapshot(), c_source);
  {
    DialogAnswers dialog({QMessageBox::Save, QMessageBox::Cancel});
    QVERIFY(!window->aboutToClose(false));
    QVERIFY(dialog.accepted());
  }
  QCOMPARE(window->getLatestContent(), draft);
  QVERIFY(window->isModified());
  {
    DialogAnswers dialog({QMessageBox::Discard});
    QVERIFY(!window->reload());
    QVERIFY(dialog.accepted());
  }
  QCOMPARE(window->getLatestContent(), draft);
  QVERIFY(window->isModified());
  {
    QMutexLocker lock(&store->mutex);
    store->saveError = VXCORE_OK;
    store->loadError = VXCORE_OK;
  }
  QVERIFY(window->save());
  QCOMPARE(store->snapshot(), draft.toUtf8());
  QVERIFY(!window->isModified());
  QVERIFY(window->save());
  QCOMPARE(store->snapshot(), draft.toUtf8());
}

void TestTodoViewWindow::dirtyReplacement_data() {
  QTest::addColumn<int>("answer");
  QTest::newRow("cancel-keeps-binding-and-undo") << int(QMessageBox::Cancel);
  QTest::newRow("save-then-rebind") << int(QMessageBox::Save);
  QTest::newRow("discard-then-rebind") << int(QMessageBox::Discard);
}

void TestTodoViewWindow::dirtyReplacement() {
  QFETCH(int, answer);
  createArea();
  m_config->getCoreConfig().setAlwaysOpenNotesInNewTabs(false);
  const auto first = openNote(QStringLiteral("replace-first.todo"));
  QVERIFY(first.isValid());
  auto *window = current();
  QVERIFY(window && openTask(window, QStringLiteral("id:alpha")));
  QVERIFY(typeText(control<QLineEdit>(window, "todoTitle"), QStringLiteral("Outgoing draft")));
  const auto draft = window->getLatestContent();
  const auto undoSteps = window->getSourceEditor()->document()->availableUndoSteps();
  const QByteArray incoming = "# Default\n\n## Incoming\n\n- [ ] Replacement task\n";
  DialogAnswers dialog({static_cast<QMessageBox::StandardButton>(answer)});
  const auto second = openNote(QStringLiteral("replace-second.todo"), incoming);
  QVERIFY(dialog.accepted());
  QCOMPARE(current(), window);
  QCOMPARE(m_area->getCurrentViewSplit()->getViewWindowCount(), 1);
  if (answer == int(QMessageBox::Cancel)) {
    QCOMPARE(window->getBuffer().id(), first.id());
    QCOMPARE(window->getLatestContent(), draft);
    QCOMPARE(window->getSourceEditor()->document()->availableUndoSteps(), undoSteps);
    QVERIFY(window->isModified());
    // Cancellation must restore controller binding/editability, not only pixels.
    focusTasks(window);
    QVERIFY(quickAdd(window, QStringLiteral("Still the outgoing document")));
  } else {
    QVERIFY(second.isValid());
    QCOMPARE(window->getBuffer().id(), second.id());
    QCOMPARE(window->getLatestContent().toUtf8(), incoming);
    QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
    QVERIFY(!window->getController()->getDocument().findItem(QStringLiteral("id:alpha")));
    QVERIFY(quickAdd(window, QStringLiteral("Only the replacement")));
    QVERIFY(window->save());
    QVERIFY(readBytes(absolute(QStringLiteral("replace-second.todo")))
                .contains("Only the replacement"));
  }
  QCOMPARE(readBytes(absolute(QStringLiteral("replace-first.todo"))),
           answer == int(QMessageBox::Save) ? draft.toUtf8() : c_source);
}

void TestTodoViewWindow::readOnlyNavigation_data() {
  QTest::addColumn<bool>("bufferReadOnly");
  QTest::newRow("explicit-read-mode") << false;
  QTest::newRow("read-only-buffer-in-edit-mode") << true;
}

void TestTodoViewWindow::readOnlyNavigation() {
  QFETCH(bool, bufferReadOnly);
  createArea();
  auto source =
      memorySource(QStringLiteral("Read-only planner"),
                   c_source + "\n# Projects\n<!-- todo-group: {\"id\":\"projects\"} -->\n\n"
                              "## Grouped\n<!-- todo-list: {\"id\":\"grouped\"} -->\n");
  source.readOnly = bufferReadOnly;
  FileOpenSettings settings;
  settings.m_mode = bufferReadOnly ? ViewWindowMode::Edit : ViewWindowMode::Read;
  settings.m_forceMode = true;
  QVERIFY(m_buffers->openMemoryBuffer(source, settings).isValid());
  auto *window = current();
  QVERIFY(window);
  window->applyFileOpenSettings(FileOpenSettings());
  QCOMPARE(window->getMode(), settings.m_mode);
  const auto before = window->getLatestContent();
  QVERIFY(openTask(window, QStringLiteral("id:alpha")));
  auto *title = control<QLineEdit>(window, "todoTitle");
  auto *add = control<QLineEdit>(window, "todoAddTask");
  auto *important = control<QToolButton>(window, "todoImportant");
  auto *note = control<SimpleMarkdownEditor>(window, "todoNote");
  QVERIFY(title && add && important && note);
  QVERIFY(title->isReadOnly() || !title->isEnabled());
  QVERIFY(add->isReadOnly() || !add->isEnabled());
  QVERIFY(!important->isEnabled());
  QVERIFY(note->isReadOnly());
  auto *newGroup = control<QPushButton>(window, "todoNewGroup");
  QVERIFY(newGroup && !newGroup->isEnabled());
  window->getTodoEditor()->focusGroup(QStringLiteral("id:projects"));
  QVERIFY(toggleGroupInSidebar(window));
  QVERIFY(window->getController()->getProjection().m_collapsedGroups.contains(
      QStringLiteral("id:projects")));
  auto *lists = control<QListView>(window, "todoLists");
  auto *model = window->getController()->getListModel();
  QVERIFY(lists->isRowHidden(model->rowForKey(QStringLiteral("id:grouped"))));
  QVERIFY(!lists->isRowHidden(model->rowForKey(QStringLiteral("id:work"))));
  QCOMPARE(model->rowForGroup(QStringLiteral("id:default")), -1);
  QVERIFY(!window->getController()->renameGroup(QStringLiteral("id:projects"),
                                                QStringLiteral("Blocked")));
  QVERIFY(!window->getController()->ungroupLists(QStringLiteral("id:projects")));
  QCOMPARE(window->getLatestContent(), before);
  QVERIFY(!window->getController()->removeTask(QStringLiteral("id:alpha")));
  QVERIFY(selectView(window, TodoViewKind::All));
  auto *search = control<QLineEdit>(window, "todoSearch");
  QVERIFY(typeText(search, QStringLiteral("Gamma")));
  QCOMPARE(taskTitles(window), QStringList({QStringLiteral("Gamma")}));
  QVERIFY(window->getSourceEditor()->isHidden());
  QVERIFY(window->getTodoEditor()->isVisible());
  QCOMPARE(window->getLatestContent(), before);
  QVERIFY(!window->isModified());
  if (!bufferReadOnly) {
    search->clear();
    window->setMode(ViewWindowMode::Edit);
    QVERIFY(quickAdd(window, QStringLiteral("Now editable")));
    const auto edited = window->getLatestContent();
    window->setNoteConversionFrozen(true);
    QVERIFY(!window->getController()->removeTask(QStringLiteral("id:alpha")));
    window->undo();
    QCOMPARE(window->getLatestContent(), edited);
    window->setNoteConversionFrozen(false);
    window->setEnabled(false);
    QVERIFY(!window->getController()->addList(QStringLiteral("Disabled")));
    window->setEnabled(true);
    QCOMPARE(window->getLatestContent(), edited);
  }
}

void TestTodoViewWindow::sharedBufferRejectsStaleGestures() {
  createArea();
  const auto buffer = openNote(QStringLiteral("shared.todo"));
  QVERIFY(buffer.isValid());
  auto *first = current();
  QVERIFY(first);
  const auto firstWorkspace = m_area->getController()->getCurrentWorkspaceId();
  focusTasks(first);
  m_area->getController()->splitViewSplit(firstWorkspace, Direction::Right, true);
  auto *second = current();
  QVERIFY(second && second != first);
  QCOMPARE(second->getBuffer().id(), buffer.id());
  const auto secondWorkspace = m_area->getController()->getCurrentWorkspaceId();
  const auto secondModel = second->getController()->getTaskModel();
  m_area->getController()->focusWindow(firstWorkspace, buffer.id());
  focusTasks(first);
  QVERIFY(quickAdd(first, QStringLiteral("Written in first split")));
  const auto autoSaved = first->getLatestContent();
  auto *timer = m_buffers->asQObject()->findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
  QVERIFY(timer);
  timer->setInterval(60 * 60 * 1000);
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()) && !first->isModified());
  QCOMPARE(readBytes(absolute(QStringLiteral("shared.todo"))), autoSaved.toUtf8());
  // Focus alone must refresh the inactive source after another window's autosave:
  // do not let an earlier rejected gesture accidentally perform this refresh.
  m_area->getController()->focusWindow(secondWorkspace, buffer.id());
  focusTasks(second);
  QTRY_COMPARE(second->getSourceEditor()->toPlainText(), autoSaved);
  QVERIFY(taskTitles(second).contains(QStringLiteral("Written in first split")));
  QVERIFY(!second->getSourceEditor()->document()->isUndoAvailable());
  const auto staleIndex = secondModel->index(secondModel->rowForKey(QStringLiteral("id:alpha")));
  const int staleRevision = secondModel->getRevision();
  std::unique_ptr<QMimeData> staleDrag(secondModel->mimeData({staleIndex}));
  m_area->getController()->focusWindow(firstWorkspace, buffer.id());
  focusTasks(first);
  QVERIFY(quickAdd(first, QStringLiteral("Another foreign edit")));
  const auto authoritative = first->getLatestContent();
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()) && !first->isModified());
  QCOMPARE(readBytes(absolute(QStringLiteral("shared.todo"))), authoritative.toUtf8());
  m_buffers->setAutoSavePolicy(AutoSavePolicy::None);
  QSignalSpy rejected(second->getController(), &TodoViewWindowController::operationRejected);
  secondModel->checkedChangeRequested(QStringLiteral("id:alpha"), true, staleRevision);
  QVERIFY(!rejected.isEmpty());
  QCOMPARE(buffer.getContentRaw(), authoritative.toUtf8());
  QVERIFY(!secondModel->dropMimeData(staleDrag.get(), Qt::MoveAction, 0, 0, QModelIndex()));
  m_area->getController()->focusWindow(secondWorkspace, buffer.id());
  focusTasks(second);
  QTRY_COMPARE(second->getLatestContent(), authoritative);
  QVERIFY(!second->getController()
               ->getDocument()
               .findItem(QStringLiteral("id:alpha"))
               ->m_fields.m_checked);
  QVERIFY(!second->getSourceEditor()->document()->isUndoAvailable());
  secondModel->setData(secondModel->index(secondModel->rowForKey(QStringLiteral("id:alpha"))),
                       Qt::Checked, Qt::CheckStateRole);
  QVERIFY(second->getController()
              ->getDocument()
              .findItem(QStringLiteral("id:alpha"))
              ->m_fields.m_checked);
  QVERIFY(m_buffers->syncNow(buffer.id()));
  const auto latest = second->getLatestContent();
  m_area->getController()->focusWindow(firstWorkspace, buffer.id());
  focusTasks(first);
  QTRY_COMPARE(first->getLatestContent(), latest);
  QVERIFY(first->save());
  QCOMPARE(readBytes(absolute(QStringLiteral("shared.todo"))), latest.toUtf8());
}

void TestTodoViewWindow::foreignReloadAndSameTypeReplacement() {
  createArea();
  const auto buffer = openNote(QStringLiteral("reload.todo"));
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window && openTask(window, QStringLiteral("id:alpha")));
  auto *title = control<QLineEdit>(window, "todoTitle");
  QVERIFY(typeText(title, QStringLiteral("Native draft")));
  QVERIFY(window->save());
  const auto model = window->getController()->getTaskModel();
  const auto index = model->index(model->rowForKey(QStringLiteral("id:alpha")));
  const auto oldRevision = model->getRevision();
  std::unique_ptr<QMimeData> payload(model->mimeData({index}));
  auto replacement = c_source;
  replacement.replace("Alpha", "Authoritative reload");
  QVERIFY(writeBytes(absolute(QStringLiteral("reload.todo")), replacement));
  QVERIFY(window->reload());
  QCOMPARE(window->getLatestContent().toUtf8(), replacement);
  QCOMPARE(title->text(), QStringLiteral("Authoritative reload"));
  QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
  QSignalSpy rejected(window->getController(), &TodoViewWindowController::operationRejected);
  model->checkedChangeRequested(QStringLiteral("id:alpha"), true, oldRevision);
  QVERIFY(!rejected.isEmpty());
  QCOMPARE(window->getLatestContent().toUtf8(), replacement);
  QVERIFY(!model->dropMimeData(payload.get(), Qt::MoveAction, 0, 0, QModelIndex()));
  QCOMPARE(window->getLatestContent().toUtf8(), replacement);
  // Reuse through the real ViewArea path, not a copied replacement hook.
  m_config->getCoreConfig().setAlwaysOpenNotesInNewTabs(false);
  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Edit;
  const QByteArray nextSource = "# Default\n\n## Next\n\n- [ ] New binding\n";
  const auto next = openNote(QStringLiteral("next.todo"), nextSource, settings);
  QVERIFY(next.isValid());
  QCOMPARE(current(), window);
  QCOMPARE(window->getBuffer().id(), next.id());
  QVERIFY(window->supportsBufferReplacement());
  QCOMPARE(window->getLatestContent().toUtf8(), nextSource);
  QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
  QVERIFY(!window->getController()->getDocument().findItem(QStringLiteral("id:alpha")));
  QVERIFY(!window->getController()->removeTask(QStringLiteral("id:alpha")));
  model->checkedChangeRequested(QStringLiteral("id:alpha"), true, oldRevision);
  QCOMPARE(window->getLatestContent().toUtf8(), nextSource);
  QVERIFY(quickAdd(window, QStringLiteral("Only new buffer")));
  QVERIFY(window->save());
  QCOMPARE(readBytes(absolute(QStringLiteral("reload.todo"))), replacement);
  QVERIFY(readBytes(absolute(QStringLiteral("next.todo"))).contains("Only new buffer"));
}

void TestTodoViewWindow::nativeLayoutsKeyboardAndTheme() {
  qApp->setStyleSheet(m_theme->fetchQtStyleSheet());
  connect(m_theme.get(), &ThemeService::themeChanged, this,
          [this]() { qApp->setStyleSheet(m_theme->fetchQtStyleSheet()); });
  createArea();
  QVERIFY(openNote(QStringLiteral("layout.todo")).isValid());
  auto *window = current();
  QVERIFY(window);
  auto *sidebar = control<QWidget>(window, "todoSidebar");
  auto *listsButton = control<QPushButton>(window, "todoListsButton");
  auto *tasks = control<QListView>(window, "todoTasks");
  auto *splitter = control<QSplitter>(window, "todoSplitter");
  auto *back = control<QToolButton>(window, "todoBack");
  QVERIFY(sidebar && listsButton && tasks && splitter && back);
  QTRY_VERIFY(sidebar->isVisible());
  QVERIFY(window->getTodoEditor()->width() >= 840);
  QVERIFY(!listsButton->isVisible());
  auto *taskPane = control<QWidget>(window, "todoTaskPane");
  QVERIFY(taskPane);
  auto *model = window->getController()->getTaskModel();
  QVERIFY(openTask(window, QStringLiteral("id:alpha")));
  QTRY_VERIFY(
      selectedTitleReadable(tasks, model->index(model->rowForKey(QStringLiteral("id:alpha")))));
  QTRY_VERIFY(control<QWidget>(window, "todoDetails")->isVisible());
  QVERIFY(tasks->isVisible());
  auto *detailsScroll = control<QScrollArea>(window, "todoDetailsScroll");
  QVERIFY(detailsScroll);
  QTRY_COMPARE(detailsScroll->horizontalScrollBar()->maximum(), 0);
  for (const char *name : {"todoStepChecked_0", "todoStepTitle_0", "todoStepMore_0", "todoDue"}) {
    auto *action = control<QWidget>(window, name);
    QVERIFY(action);
    const auto left = action->mapTo(detailsScroll->viewport(), QPoint()).x();
    QVERIFY2(left >= 0 && left + action->width() <= detailsScroll->viewport()->width(), name);
  }
  const auto source = window->getLatestContent();
  const auto selection = window->getController()->getSelectedItemKey();
  m_area->resize(600, 760);
  QTRY_VERIFY(!sidebar->isVisible());
  QVERIFY(window->getTodoEditor()->width() < 840);
  QTRY_VERIFY(listsButton->isVisible());
  QVERIFY(back->isVisible());
  QVERIFY(!tasks->isVisible());
  QVERIFY(activate(back));
  QTest::keyClick(back, Qt::Key_Space);
  QTRY_VERIFY(tasks->isVisible());
  QVERIFY(tasks->hasFocus());
  QCOMPARE(window->getController()->getSelectedItemKey(), selection);
  QTest::keyClick(tasks, Qt::Key_F, Qt::ControlModifier);
  auto *search = control<QLineEdit>(window, "todoSearch");
  QVERIFY(search);
  QTRY_VERIFY(search->hasFocus() && sidebar->isVisible());
  QInputMethodEvent preedit(QStringLiteral("compose"), {});
  QCoreApplication::sendEvent(search, &preedit);
  QTest::keyClick(search, Qt::Key_Escape);
  QVERIFY(sidebar->isVisible());
  QInputMethodEvent clearPreedit;
  QCoreApplication::sendEvent(search, &clearPreedit);
  QTest::keyClick(search, Qt::Key_Escape);
  QTRY_VERIFY(!sidebar->isVisible() && listsButton->hasFocus());
  const auto paneGeometry = taskPane->geometry();
  const auto panePosition = taskPane->mapTo(window->getTodoEditor(), QPoint());
  QTest::mouseClick(listsButton, Qt::LeftButton);
  QTRY_VERIFY(sidebar->isVisible());
  QCOMPARE(taskPane->geometry(), paneGeometry);
  QCOMPARE(taskPane->mapTo(window->getTodoEditor(), QPoint()), panePosition);
  QTest::mouseClick(listsButton, Qt::LeftButton);
  QTRY_VERIFY(!sidebar->isVisible());
  QVERIFY(!listsButton->isChecked());
  QTest::mouseClick(listsButton, Qt::LeftButton);
  QTRY_VERIFY(sidebar->isVisible());
  const QPoint outside(tasks->viewport()->width() - 5, tasks->viewport()->height() - 5);
  QTest::mouseClick(tasks->viewport(), Qt::LeftButton, Qt::NoModifier, outside);
  QTRY_VERIFY(!sidebar->isVisible());
  QVERIFY(!listsButton->isChecked());
  window->getTodoEditor()->focusSearch();
  QTRY_VERIFY(search->hasFocus());
  QTest::keyClick(search, Qt::Key_Tab);
  QVERIFY(QApplication::focusWidget());
  QVERIFY(QApplication::focusWidget() != search);
  QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab);
  QTRY_VERIFY(search->hasFocus());
  window->getTodoEditor()->focusTasks();
  QTRY_VERIFY(tasks->hasFocus());
  QTest::keyClick(tasks, Qt::Key_Return);
  QTRY_VERIFY(back->isVisible());
  auto *title = control<QLineEdit>(window, "todoTitle");
  QVERIFY(title && activate(title));
  QTest::keyClick(title, Qt::Key_F, Qt::ControlModifier);
  QTRY_VERIFY(tasks->isVisible() && !detailsScroll->isVisible());
  QTRY_VERIFY(sidebar->isVisible() && search->hasFocus());
  QTest::keyClick(search, Qt::Key_Return);
  QTRY_VERIFY(!sidebar->isVisible() && tasks->hasFocus());
  QVERIFY(openTask(window, selection));
  QTest::keyClick(title, Qt::Key_Escape);
  QTRY_VERIFY(tasks->isVisible() && tasks->hasFocus());
  window->getTodoEditor()->focusSearch();
  QTRY_VERIFY(sidebar->isVisible());
  const auto light = window->getSourceEditor()->getTextEdit()->palette().color(QPalette::Text);
  m_theme->switchTheme(QStringLiteral("everforest-dark"));
  QTRY_VERIFY(window->getSourceEditor()->getTextEdit()->palette().color(QPalette::Text) != light);
  m_theme->refreshCurrentTheme();
  m_area->resize(1000, 760);
  QTRY_VERIFY(sidebar->isVisible() && !listsButton->isVisible());
  QVERIFY(!listsButton->isChecked());
  QCOMPARE(splitter->widget(0), sidebar);
  window->getTodoEditor()->focusTasks();
  QVERIFY(sidebar->isVisible());
  QTRY_VERIFY(
      selectedTitleReadable(tasks, model->index(model->rowForKey(QStringLiteral("id:alpha")))));
  // Native's inactive alternate-base can be transparent black; selection must stay readable.
  m_theme->switchTheme(QStringLiteral("native"));
  QVERIFY(activate(tasks));
  {
    QLineEdit otherWindow;
    otherWindow.resize(200, 40);
    otherWindow.show();
    QVERIFY(activate(&otherWindow));
    QTRY_VERIFY(!m_area->isActiveWindow());
    QTRY_VERIFY(
        selectedTitleReadable(tasks, model->index(model->rowForKey(QStringLiteral("id:alpha")))));
  }
  QVERIFY(activate(tasks));
  m_theme->switchTheme(QStringLiteral("pure"));
  QTRY_COMPARE(window->getSourceEditor()->getTextEdit()->palette().color(QPalette::Text), light);
  QCOMPARE(window->getLatestContent(), source);
  QCOMPARE(window->getController()->getSelectedItemKey(), selection);
  QVERIFY(!window->isModified());
  const int zoom = window->getTodoEditor()->getZoomDelta();
  window->getTodoEditor()->zoom(1);
  QCOMPARE(window->getTodoEditor()->getZoomDelta(), zoom + 1);
  QCOMPARE(window->getLatestContent(), source);
  QVERIFY(activate(tasks));
  QTest::keyClick(tasks, Qt::Key_Space);
  QVERIFY(window->getController()
              ->getDocument()
              .findItem(QStringLiteral("id:alpha"))
              ->m_fields.m_checked);
  window->undo();
  QCOMPARE(window->getLatestContent(), source);
  m_area->resize(600, 760);
  QTRY_VERIFY(listsButton->isVisible());
  QVERIFY(selectView(window, TodoViewKind::List, QStringLiteral("id:personal")));
  QVERIFY(quickAdd(window, QStringLiteral("Added in a narrow split")));
  const auto narrowKey = window->getController()->getSelectedItemKey();
  QVERIFY(openTask(window, narrowKey));
  QVERIFY(typeText(title, QStringLiteral("Edited in narrow details")));
  QCOMPARE(window->getController()->getDocument().findItem(narrowKey)->m_fields.m_title,
           QStringLiteral("Edited in narrow details"));
  window->undo();
  QVERIFY(window->getSourceEditor()->isHidden());
  QCOMPARE(window->getController()->getDocument().findItem(narrowKey)->m_fields.m_title,
           QStringLiteral("Added in a narrow split"));
  QTRY_VERIFY(back->isVisible());
  QVERIFY(activate(back));
  QTest::keyClick(back, Qt::Key_Space);
  QVERIFY(tasks->isVisible() && tasks->hasFocus());
  for (const char *name :
       {"todoSearch", "todoAddTask", "todoTitle", "todoChecked", "todoImportant"}) {
    auto *widget = control<QWidget>(window, name);
    QVERIFY(widget);
    QVERIFY2(!widget->accessibleName().isEmpty(), name);
  }
}

void TestTodoViewWindow::groupSourceNavigation_data() {
  QTest::addColumn<QString>("needle");
  QTest::addColumn<int>("width");
  QTest::addColumn<QString>("groupKey");
  QTest::newRow("wide-heading") << QStringLiteral("# Empty") << 1000 << QStringLiteral("id:empty");
  QTest::newRow("wide-comment") << QStringLiteral("Empty group comment") << 1000
                                << QStringLiteral("id:empty");
  QTest::newRow("narrow-heading") << QStringLiteral("# Empty") << 600 << QStringLiteral("id:empty");
  QTest::newRow("narrow-comment") << QStringLiteral("Empty group comment") << 600
                                  << QStringLiteral("id:empty");
  QTest::newRow("wide-default-heading")
      << QStringLiteral("# Default") << 1000 << QStringLiteral("id:default");
  QTest::newRow("wide-default-comment")
      << QStringLiteral("Default group comment") << 1000 << QStringLiteral("id:default");
  QTest::newRow("narrow-default-heading")
      << QStringLiteral("# Default") << 600 << QStringLiteral("id:default");
  QTest::newRow("narrow-default-comment")
      << QStringLiteral("Default group comment") << 600 << QStringLiteral("id:default");
}

void TestTodoViewWindow::groupSourceNavigation() {
  QFETCH(QString, needle);
  QFETCH(int, width);
  QFETCH(QString, groupKey);
  createArea();
  const auto source =
      c_source +
      QByteArray(
          "\n# Empty\n<!-- todo-group: {\"id\":\"empty\"} -->\n\n<!-- Empty group comment -->\n");
  FileOpenSettings readMode;
  readMode.m_mode = ViewWindowMode::Read;
  readMode.m_forceMode = true;
  QVERIFY(openNote(QStringLiteral("group-navigation.todo"), source, readMode).isValid());
  auto *window = current();
  QVERIFY(window);
  m_area->resize(width, 760);
  QTRY_COMPARE(window->getTodoEditor()->width() < 840, width < 840);
  auto *controller = window->getController();
  controller->setView(TodoViewKind::Important);
  controller->setSearch(QStringLiteral("Beta"));
  controller->setSelectedItemKey(QStringLiteral("id:beta"));
  controller->setGroupExpanded(QStringLiteral("id:default"), false);
  const auto before = controller->getProjection();
  const auto items = taskTitles(window);
  const auto text = QString::fromUtf8(source);
  FileOpenSettings settings;
  settings.m_lineNumber = text.left(text.indexOf(needle)).count(QLatin1Char('\n'));
  window->applyFileOpenSettings(settings);
  QCOMPARE(window->getTodoEditor()->selectedGroupKey(), groupKey);
  QCOMPARE(controller->getProjection().m_view, before.m_view);
  QCOMPARE(controller->getProjection().m_listKey, before.m_listKey);
  QCOMPARE(controller->getProjection().m_search, before.m_search);
  QCOMPARE(controller->getProjection().m_tag, before.m_tag);
  QCOMPARE(controller->getProjection().m_collapsedGroups, before.m_collapsedGroups);
  QCOMPARE(taskTitles(window), items);
  QVERIFY(controller->getSelectedItemKey().isEmpty());
  auto *lists = control<QListView>(window, "todoLists");
  QTRY_VERIFY(lists && lists->isVisible() && lists->hasFocus());
  const bool defaultOwner = groupKey == QLatin1String("id:default");
  QCOMPARE(lists->currentIndex().data(TodoListModel::GroupRole).toBool(), !defaultOwner);
  QCOMPARE(lists->currentIndex().data(TodoListModel::IdRole).toString(),
           defaultOwner ? QStringLiteral("id:work") : groupKey);
  if (defaultOwner) {
    QCOMPARE(lists->currentIndex().row(), 4);
    QVERIFY(lists->currentIndex().data(TodoListModel::GroupIdRole).toString().isEmpty());
    QVERIFY(!lists->isRowHidden(4));
  }
  QSignalSpy taskReset(controller->getTaskModel(), &QAbstractItemModel::modelReset);
  controller->setGroupExpanded(QStringLiteral("id:empty"), false);
  QCOMPARE(window->getTodoEditor()->selectedGroupKey(), groupKey);
  QCOMPARE(lists->currentIndex().data(TodoListModel::IdRole).toString(),
           defaultOwner ? QStringLiteral("id:work") : groupKey);
  QCOMPARE(controller->getProjection().m_view, before.m_view);
  QCOMPARE(controller->getProjection().m_listKey, before.m_listKey);
  QCOMPARE(controller->getProjection().m_search, before.m_search);
  QCOMPARE(controller->getProjection().m_tag, before.m_tag);
  QCOMPARE(taskTitles(window), items);
  QCOMPARE(taskReset.size(), 0);
  settings.m_lineNumber =
      text.left(text.indexOf(QStringLiteral("- [ ] Gamma"))).count(QLatin1Char('\n'));
  window->applyFileOpenSettings(settings);
  QCOMPARE(controller->getProjection().m_listKey, QStringLiteral("id:personal"));
  QCOMPARE(controller->getSelectedItemKey(), QStringLiteral("id:gamma"));
  QCOMPARE(controller->getProjection().m_groupKey, QStringLiteral("id:default"));
  QVERIFY(!controller->getProjection().m_collapsedGroups.contains(QStringLiteral("id:default")));
  QVERIFY(controller->getProjection().m_search.isEmpty());
  QCOMPARE(window->getMode(), ViewWindowMode::Read);
  QCOMPARE(window->getLatestContent().toUtf8(), source);
  QVERIFY(window->getSourceEditor()->isHidden());
  QVERIFY(!window->isModified());
}

void TestTodoViewWindow::tasksOnlyNavigation_data() {
  QTest::addColumn<QString>("needle");
  QTest::addColumn<int>("bias");
  QTest::addColumn<int>("route"); // 0: line, 1: search line, 2: cursor offset, 3: invalid line.
  QTest::addColumn<QString>("listKey");
  QTest::addColumn<QString>("itemKey");
  QTest::newRow("another-list") << QStringLiteral("- [ ] Gamma") << 0 << 0
                                << QStringLiteral("id:personal") << QStringLiteral("id:gamma");
  QTest::newRow("completed-task") << QStringLiteral("- [x] Done") << 0 << 0
                                  << QStringLiteral("id:work") << QStringLiteral("id:done");
  QTest::newRow("step") << QStringLiteral("Second step") << 0 << 0 << QStringLiteral("id:work")
                        << QStringLiteral("id:alpha");
  QTest::newRow("note") << QStringLiteral("Existing **note**") << 0 << 0
                        << QStringLiteral("id:work") << QStringLiteral("id:alpha");
  QTest::newRow("task-metadata") << QStringLiteral("opaque-metadata-only") << 0 << 0
                                 << QStringLiteral("id:work") << QStringLiteral("id:alpha");
  QTest::newRow("list-heading") << QStringLiteral("## Personal") << 0 << 0
                                << QStringLiteral("id:personal") << QString();
  QTest::newRow("list-description")
      << QStringLiteral("> Team commitments") << 0 << 0 << QStringLiteral("id:work") << QString();
  QTest::newRow("list-blank-line")
      << QStringLiteral("\n\n- [ ] Alpha") << 1 << 0 << QStringLiteral("id:work") << QString();
  QTest::newRow("list-comment") << QStringLiteral("Work-scoped separator") << 0 << 0
                                << QStringLiteral("id:work") << QString();
  QTest::newRow("search-line-precedence") << QStringLiteral("+ [ ] Beta") << 0 << 1
                                          << QStringLiteral("id:work") << QStringLiteral("id:beta");
  QTest::newRow("utf16-offset-precedence")
      << QStringLiteral("Gamma") << 2 << 2 << QStringLiteral("id:personal")
      << QStringLiteral("id:gamma");
  QTest::newRow("file-comment-no-owner")
      << QStringLiteral("File-scoped author comment") << 0 << 0 << QString() << QString();
  QTest::newRow("out-of-range-line") << QStringLiteral("Gamma") << 0 << 3 << QString() << QString();
}

void TestTodoViewWindow::tasksOnlyNavigation() {
  QFETCH(QString, needle);
  QFETCH(int, bias);
  QFETCH(int, route);
  QFETCH(QString, listKey);
  QFETCH(QString, itemKey);
  createArea();
  auto text = QString::fromUtf8(c_source);
  text.replace(QStringLiteral("## Work"), QString::fromUtf8("## 工作 😀 Work"));
  FileOpenSettings readMode;
  readMode.m_forceMode = true;
  readMode.m_mode = ViewWindowMode::Read;
  QVERIFY(openNote(QStringLiteral("navigate.todo"), text.toUtf8(), readMode).isValid());
  auto *window = current();
  QVERIFY(window);
  focusTasks(window);
  auto *controller = window->getController();
  controller->setView(TodoViewKind::MyDay);
  controller->setSearch(QStringLiteral("no matching task"));
  controller->setTagFilter(QStringLiteral("missing tag"));
  controller->setCompletedExpanded(false);
  const auto before = controller->getProjection();
  const auto selected = controller->getSelectedItemKey();
  const int offset = text.indexOf(needle) + bias;
  QVERIFY(offset >= 0);
  const int line = text.left(offset).count(QLatin1Char('\n'));
  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Edit; // Unforced navigation must preserve Read.
  settings.m_lineNumber = line;
  if (route == 1 || route == 2) {
    settings.m_lineNumber = 0;
    settings.m_searchHighlight.m_isValid = true;
    settings.m_searchHighlight.m_patterns =
        QStringList{QStringLiteral("metadata-not-a-task-filter")};
    settings.m_searchHighlight.m_currentMatchLine = route == 2 ? 0 : line;
  }
  if (route == 2)
    settings.m_cursorOffset = offset;
  if (route == 3)
    settings.m_lineNumber = 99999;
  window->applyFileOpenSettings(settings);
  QCOMPARE(window->getMode(), ViewWindowMode::Read);
  QCOMPARE(window->getLatestContent(), text);
  QVERIFY(!window->isModified());
  QVERIFY(window->getTodoEditor()->isVisible());
  QVERIFY(window->getSourceEditor()->isHidden());
  if (listKey.isEmpty()) {
    QCOMPARE(controller->getProjection().m_view, before.m_view);
    QCOMPARE(controller->getProjection().m_search, before.m_search);
    QCOMPARE(controller->getProjection().m_tag, before.m_tag);
    QCOMPARE(controller->getSelectedItemKey(), selected);
    return;
  }
  QCOMPARE(controller->getProjection().m_view, TodoViewKind::List);
  QCOMPARE(controller->getProjection().m_listKey, listKey);
  QVERIFY(controller->getProjection().m_search.isEmpty());
  QVERIFY(controller->getProjection().m_tag.isEmpty());
  QCOMPARE(controller->getSelectedItemKey(), itemKey);
  if (!itemKey.isEmpty()) {
    const int row = controller->getTaskModel()->rowForKey(itemKey);
    QVERIFY(row >= 0);
    QCOMPARE(control<QListView>(window, "todoTasks")->currentIndex().row(), row);
  }
}

void TestTodoViewWindow::tasksOnlyFindAndReplace() {
  createArea();
  QVERIFY(openNote(QStringLiteral("tasks-search.todo")).isValid());
  auto *window = current();
  QVERIFY(window);
  focusTasks(window);
  auto *search = control<QLineEdit>(window, "todoSearch");
  auto *tasks = control<QListView>(window, "todoTasks");
  QVERIFY(search && tasks);
  QTest::keyClick(tasks, Qt::Key_F, Qt::ControlModifier);
  QTRY_VERIFY(search->hasFocus());
  window->findNext(QStringLiteral("Gamma"), FindNone);
  QCOMPARE(search->text(), QStringLiteral("Gamma"));
  QCOMPARE(taskTitles(window), QStringList{QStringLiteral("Gamma")});
  window->replace(QStringLiteral("Gamma"), FindNone, QStringLiteral("Not a raw edit"));
  window->replaceAll(QStringLiteral("Gamma"), FindNone, QStringLiteral("Not a raw edit"));
  QTest::keyClick(search, Qt::Key_H, Qt::ControlModifier);
  QCOMPARE(window->getLatestContent().toUtf8(), c_source);
  QVERIFY(window->getSourceEditor()->isHidden());
  QVERIFY(window->getTodoEditor()->isVisible());
  QVERIFY(!window->isModified());
  QVERIFY(openTask(window, QStringLiteral("id:gamma")));
  auto *title = control<QLineEdit>(window, "todoTitle");
  QVERIFY(title && activate(title));
  title->setSelection(0, 3);
  QCOMPARE(window->getSelectedText(), QStringLiteral("Gam"));
  QVERIFY(!window->isPrintSupported());
}

void TestTodoViewWindow::unavailableSourceIsNotEmpty() {
  const auto buffer = m_buffers->openMemoryBuffer(memorySource(QStringLiteral("Retired source")));
  QVERIFY(buffer.isValid());
  QVERIFY(m_buffers->closeBuffer(buffer.id()));
  TodoViewWindow window(*m_services, buffer);
  QString content = QStringLiteral("sentinel");
  QString error;
  QVERIFY(!window.tryGetLatestContent(&content, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(!window.getController()->addList(QStringLiteral("No invented source")));
  QVERIFY(!window.save());
}

void TestTodoViewWindow::plannedDueDoesNotLeak() {
  createArea();
  QVERIFY(openNote(QStringLiteral("due-scope.todo")).isValid());
  auto *window = current();
  QVERIFY(window);
  focusTasks(window);
  auto *controller = window->getController();
  auto *chosen = control<QCheckBox>(window, "todoAddHasDue");
  auto *date = control<QDateEdit>(window, "todoAddDue");
  QVERIFY(chosen && date);
  struct Destination {
    TodoViewKind view;
    QString search;
    QString tag;
  };
  const QList<Destination> destinations{
      {TodoViewKind::List, {}, {}},
      {TodoViewKind::All, {}, {}},
      {TodoViewKind::MyDay, {}, {}},
      {TodoViewKind::Important, {}, {}},
      {TodoViewKind::Planned, QStringLiteral("not-matching"), {}},
      {TodoViewKind::Planned, {}, QStringLiteral("work")},
      {TodoViewKind::Planned, QStringLiteral("not-matching"), QStringLiteral("work")}};
  int number = 0;
  for (const auto &destination : destinations) {
    controller->setSearch({});
    controller->setTagFilter({});
    QVERIFY(selectView(window, TodoViewKind::Planned));
    QVERIFY(chosen->isVisible() && date->isVisible());
    chosen->setChecked(true);
    date->setDate(QDate::currentDate().addDays(9));
    const auto before = window->getSourceEditor()->toPlainText();
    QVERIFY(selectView(window, destination.view,
                       destination.view == TodoViewKind::List ? QStringLiteral("id:personal")
                                                              : QString()));
    controller->setSearch(destination.search);
    controller->setTagFilter(destination.tag);
    QVERIFY(!chosen->isVisible() && !date->isVisible());
    QCOMPARE(window->getSourceEditor()->toPlainText(), before);
    QVERIFY(quickAdd(window, QStringLiteral("No inherited due %1").arg(++number)));
    const auto *created = controller->getDocument().findItem(controller->getSelectedItemKey());
    QVERIFY(created);
    QVERIFY(!created->m_fields.m_due.isValid());
    QVERIFY(created->m_fields.m_tags.isEmpty());
  }
}

void TestTodoViewWindow::storedMyDayRemainsVisible() {
  createArea();
  QVERIFY(openNote(QStringLiteral("stored-day.todo")).isValid());
  auto *window = current();
  QVERIFY(window && openTask(window, QStringLiteral("id:alpha")));
  auto *controller = window->getController();
  const auto today = QDate::currentDate();
  controller->setToday(today);
  auto fields = controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields;
  fields.m_myDay = today.addDays(-1);
  QVERIFY(controller->updateTask(QStringLiteral("id:alpha"), fields));
  auto *day = control<QPushButton>(window, "todoMyDay");
  QVERIFY(day);
  QVERIFY(!day->isChecked());
  const auto oldDay = QLocale().toString(fields.m_myDay, QLocale::ShortFormat);
  QVERIFY(day->toolTip().contains(oldDay));
  QVERIFY(day->accessibleDescription().contains(oldDay));
  const auto before = window->getSourceEditor()->toPlainText();
  controller->setToday(today.addDays(1));
  QVERIFY(day->toolTip().contains(oldDay));
  QVERIFY(day->accessibleDescription().contains(oldDay));
  QCOMPARE(window->getSourceEditor()->toPlainText(), before);
  controller->setToday(today);
  day->click();
  QCOMPARE(controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields.m_myDay, today);
  const auto storedToday = QLocale().toString(today, QLocale::ShortFormat);
  QVERIFY(day->toolTip().contains(storedToday));
  QVERIFY(day->accessibleDescription().contains(storedToday));
  day->click();
  QVERIFY(
      !controller->getDocument().findItem(QStringLiteral("id:alpha"))->m_fields.m_myDay.isValid());
  QVERIFY(!day->toolTip().contains(storedToday));
  QVERIFY(!day->accessibleDescription().contains(storedToday));
  window->undo();
  window->undo();
  QCOMPARE(window->getSourceEditor()->toPlainText(), before);
  QVERIFY(day->toolTip().contains(oldDay));
  QVERIFY(day->accessibleDescription().contains(oldDay));
}

void TestTodoViewWindow::ownPendingSaveKeepsNewerEdits() {
  createArea();
  const auto buffer = openNote(QStringLiteral("pending-own.todo"));
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window);
  focusTasks(window);
  auto *timer = m_buffers->asQObject()->findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
  QVERIFY(timer);
  timer->setInterval(60 * 60 * 1000);
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  SaveGateBarrier barrier(*m_gate, m_notebook);
  QVERIFY(quickAdd(window, QStringLiteral("First queued task")));
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  QVERIFY(m_buffers->isSaveQueueBusy(buffer.id()));
  QVERIFY(!m_buffers->isDirty(buffer.id()));
  window->undo();
  QCOMPARE(window->getSourceEditor()->toPlainText().toUtf8(), c_source);
  QVERIFY(quickAdd(window, QStringLiteral("Newer local task")));
  const auto newest = window->getSourceEditor()->toPlainText();
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  barrier.release();
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()));
  QTRY_COMPARE(readBytes(absolute(QStringLiteral("pending-own.todo"))), newest.toUtf8());
  QCOMPARE(window->getSourceEditor()->toPlainText(), newest);
  QVERIFY(window->getSourceEditor()->document()->isUndoAvailable());
  window->undo();
  QCOMPARE(window->getSourceEditor()->toPlainText().toUtf8(), c_source);
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()));
  QTRY_COMPARE(readBytes(absolute(QStringLiteral("pending-own.todo"))), c_source);
}

void TestTodoViewWindow::manualSaveJoinsPriorAutosave_data() {
  QTest::addColumn<bool>("viewSave");
  QTest::newRow("view-save-joins-prior-autosave") << true;
  QTest::newRow("service-save-joins-prior-autosave") << false;
}

void TestTodoViewWindow::manualSaveJoinsPriorAutosave() {
  QFETCH(bool, viewSave);
  createArea();
  const auto buffer = openNote(QStringLiteral("join-autosave.todo"));
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window);
  focusTasks(window);
  auto *timer = m_buffers->asQObject()->findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
  QVERIFY(timer);
  timer->setInterval(60 * 60 * 1000);
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  SaveGateBarrier barrier(*m_gate, m_notebook);
  QVERIFY(quickAdd(window, QStringLiteral("Must be durable before Save returns")));
  const auto expected = window->getSourceEditor()->toPlainText().toUtf8();
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  QVERIFY(m_buffers->isSaveQueueBusy(buffer.id()));
  QVERIFY(!m_buffers->isDirty(buffer.id()));
  bool duplicateRan = false;
  bool duplicateSaved = false;
  bool releaseRan = false;
  QTimer duplicate;
  duplicate.setSingleShot(true);
  connect(&duplicate, &QTimer::timeout, this, [&] {
    duplicateRan = true;
    duplicateSaved = m_buffers->saveBuffer(buffer.id());
  });
  QTimer release;
  release.setSingleShot(true);
  connect(&release, &QTimer::timeout, this, [&] {
    releaseRan = true;
    barrier.release();
  });
  duplicate.start(10);
  release.start(60);
  const bool saved = viewSave ? window->save() : m_buffers->saveBuffer(buffer.id());
  QVERIFY(releaseRan); // A responsive event loop released the worker, not a GUI core write.
  QVERIFY(duplicateRan && duplicateSaved);
  QVERIFY(saved);
  QCOMPARE(readBytes(absolute(QStringLiteral("join-autosave.todo"))), expected);
  QCOMPARE(window->getSourceEditor()->toPlainText().toUtf8(), expected);
}

void TestTodoViewWindow::activeStaleSnapshotCannotAuthorizeHistory() {
  createArea();
  auto buffer = openNote(QStringLiteral("historical-snapshot.todo"));
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window);
  focusTasks(window);
  auto *timer = m_buffers->asQObject()->findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
  QVERIFY(timer);
  timer->setInterval(60 * 60 * 1000);
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  SaveGateBarrier barrier(*m_gate, m_notebook);
  QVERIFY(quickAdd(window, QStringLiteral("Actually queued")));
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  QVERIFY(m_buffers->isSaveQueueBusy(buffer.id()));
  QVERIFY(quickAdd(window, QStringLiteral("Observed, never submitted")));
  const auto observed = window->getLatestContent();
  window->undo();
  window->undo();
  QCOMPARE(window->getLatestContent().toUtf8(), c_source);
  barrier.release();
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()));
  QCOMPARE(window->getSourceEditor()->toPlainText().toUtf8(), c_source);
  // A real foreign core publication deliberately equals an earlier observation.
  // Do not call getLatestContent before Save: that used to prune the faulty history.
  QVERIFY(buffer.setContentRaw(observed.toUtf8()));
  QVERIFY(!window->save());
  QTRY_COMPARE(window->getSourceEditor()->toPlainText(), observed);
  QCOMPARE(buffer.getContentRaw(), observed.toUtf8());
  QVERIFY(!window->getSourceEditor()->document()->isUndoAvailable());
  QVERIFY(window->save());
  QCOMPARE(readBytes(absolute(QStringLiteral("historical-snapshot.todo"))), observed.toUtf8());
}

void TestTodoViewWindow::foreignPendingSplitWaitsForPublication() {
  createArea();
  const auto buffer = openNote(QStringLiteral("pending-handoff.todo"));
  QVERIFY(buffer.isValid());
  auto *first = current();
  QVERIFY(first);
  focusTasks(first);
  const auto workspace = m_area->getController()->getCurrentWorkspaceId();
  auto *timer = m_buffers->asQObject()->findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
  QVERIFY(timer);
  timer->setInterval(60 * 60 * 1000);
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  SaveGateBarrier barrier(*m_gate, m_notebook);
  QVERIFY(quickAdd(first, QStringLiteral("Pending handoff body")));
  const auto expected = first->getSourceEditor()->toPlainText();
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  QVERIFY(m_buffers->isSaveQueueBusy(buffer.id()));
  m_area->getController()->splitViewSplit(workspace, Direction::Right, true);
  auto *second = current();
  QVERIFY(second && second != first);
  QVERIFY(!second->getController()->addList(QStringLiteral("No stale mutation")));
  QString unavailable;
  QVERIFY(!second->tryGetLatestContent(&unavailable));
  barrier.release();
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()));
  QTRY_COMPARE(second->getSourceEditor()->toPlainText(), expected);
  QVERIFY(second->getController()->getDocument().isValid());
  focusTasks(second);
  QVERIFY(quickAdd(second, QStringLiteral("After handoff")));
  QVERIFY(second->save());
  QVERIFY(
      readBytes(absolute(QStringLiteral("pending-handoff.todo"))).contains("Pending handoff body"));
  QVERIFY(readBytes(absolute(QStringLiteral("pending-handoff.todo"))).contains("After handoff"));
}

void TestTodoViewWindow::foreignMemoryPublicationRetiresHistory_data() {
  QTest::addColumn<bool>("registeredWriter");
  QTest::newRow("other-todo-window") << true;
  QTest::newRow("unregistered-writer") << false;
}

void TestTodoViewWindow::foreignMemoryPublicationRetiresHistory() {
  QFETCH(bool, registeredWriter);
  createArea();
  auto store = std::make_shared<Store>();
  const auto buffer = m_buffers->openMemoryBuffer(storedSource(store));
  QVERIFY(buffer.isValid());
  auto *first = current();
  QVERIFY(first);
  focusTasks(first);
  QVERIFY(quickAdd(first, QStringLiteral("Persisted memory task")));
  const auto expected = first->getLatestContent();
  QVERIFY(first->save());
  const auto own = m_buffers->snapshotPublication(buffer.id());
  QVERIFY(own.writerToken != 0);
  QVERIFY(first->getSourceEditor()->document()->isUndoAvailable());
  QVERIFY(first->save());
  QCOMPARE(buffer.getRevision(), own.coreRevision);
  QVERIFY(first->getSourceEditor()->document()->isUndoAvailable());

  QObject foreignOwner;
  const auto foreignKey = reinterpret_cast<quintptr>(&foreignOwner);
  const auto unregister =
      qScopeGuard([&]() { m_buffers->unregisterActiveWriter(buffer.id(), foreignKey); });
  if (registeredWriter) {
    const auto workspace = m_area->getController()->getCurrentWorkspaceId();
    m_area->getController()->splitViewSplit(workspace, Direction::Right, true);
    auto *second = current();
    QVERIFY(second && second != first);
    QCOMPARE(second->getBuffer().id(), buffer.id());
    focusTasks(second);
    QVERIFY(second->save());
  } else {
    m_buffers->registerActiveWriter(buffer.id(), foreignKey, [expected]() { return expected; });
    m_buffers->markDirty(buffer.id());
    QVERIFY(m_buffers->syncNow(buffer.id()));
  }
  QCOMPARE(buffer.getRevision(), own.coreRevision);
  QCOMPARE(buffer.getContentRaw(), expected.toUtf8());
  QVERIFY(m_buffers->snapshotPublication(buffer.id()).writerToken != own.writerToken);
  QString rejected;
  QVERIFY(!first->tryGetLatestContent(&rejected));
  QTRY_VERIFY(!first->getSourceEditor()->document()->isUndoAvailable());
  QCOMPARE(first->getLatestContent(), expected);
  first->undo();
  first->redo();
  QCOMPARE(first->getSourceEditor()->toPlainText(), expected);
  QCOMPARE(store->snapshot(), expected.toUtf8());
}

void TestTodoViewWindow::ownMemoryCompletionKeepsNewerEdits() {
  createArea();
  auto store = std::make_shared<Store>();
  struct CallbackBarrier {
    QSemaphore once{1};
    QSemaphore entered;
    QSemaphore proceed;
  };
  auto barrier = std::make_shared<CallbackBarrier>();
  auto source = storedSource(store);
  source.save = [save = source.save, barrier](QByteArrayView p_bytes) {
    if (barrier->once.tryAcquire()) {
      barrier->entered.release();
      barrier->proceed.acquire();
    }
    return save(p_bytes);
  };
  const auto cleanup = qScopeGuard([&]() {
    barrier->proceed.release();
    m_buffers->shutdown();
  });
  const auto buffer = m_buffers->openMemoryBuffer(source);
  QVERIFY(buffer.isValid());
  auto *window = current();
  QVERIFY(window);
  focusTasks(window);
  auto *timer = m_buffers->asQObject()->findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
  QVERIFY(timer);
  timer->setInterval(60 * 60 * 1000);
  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  QVERIFY(quickAdd(window, QStringLiteral("Published memory task")));
  const auto published = window->getLatestContent();
  QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  QVERIFY(barrier->entered.tryAcquire(1, 5000));
  const int base = buffer.getRevision();
  QVERIFY(quickAdd(window, QStringLiteral("Newer local memory draft")));
  const auto draft = window->getLatestContent();
  barrier->proceed.release();
  QTRY_VERIFY(!m_buffers->isSaveQueueBusy(buffer.id()));
  QCOMPARE(store->snapshot(), published.toUtf8());
  QCOMPARE(buffer.getRevision(), base);
  QCOMPARE(window->getLatestContent(), draft);
  QVERIFY(window->isModified());
  QVERIFY(window->getSourceEditor()->document()->isUndoAvailable());
  window->undo();
  QCOMPARE(window->getLatestContent(), published);
  window->undo();
  QCOMPARE(window->getLatestContent().toUtf8(), c_source);
}

} // namespace tests

int main(int argc, char **argv) {
  QStandardPaths::setTestModeEnabled(true);
  vxcore_set_test_mode(1);
  QApplication app(argc, argv);
  tests::TestTodoViewWindow test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_todoviewwindow.moc"
