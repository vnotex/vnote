#include "todosticker.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <controllers/todostickercontroller.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/tododocument.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <models/todomodels.h>
#include <widgets/editors/todotaskview.h>

using namespace vnotex;

TodoSticker::TodoSticker(ServiceLocator &p_services, QWidget *p_parent)
    : Sticker(p_services, p_parent) {
  m_controller = new TodoStickerController(m_services, this);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  auto *toolbar = new QHBoxLayout();
  toolbar->setContentsMargins(4, 2, 4, 2);
  m_addTask = new QLineEdit(this);
  m_addTask->setObjectName(QStringLiteral("todoStickerAddTask"));
  m_addTask->setPlaceholderText(tr("Add Task"));
  m_addTask->setAccessibleName(tr("Add Task"));
  m_addTask->setToolTip(tr("Press Enter to add a task"));
  m_addTask->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  toolbar->addWidget(m_addTask, 1);
  m_edit = new QToolButton(this);
  m_edit->setObjectName(QStringLiteral("todoStickerEdit"));
  m_edit->setText(tr("Edit"));
  m_edit->setAccessibleName(tr("Edit"));
  m_edit->setAutoRaise(true);
  m_edit->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar->addWidget(m_edit);
  m_settings = new QToolButton(this);
  m_settings->setObjectName(QStringLiteral("todoStickerSettings"));
  m_settings->setText(tr("Settings"));
  m_settings->setAccessibleName(tr("Settings"));
  m_settings->setToolTip(tr("Choose an existing TODO file"));
  m_settings->setAutoRaise(true);
  m_settings->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar->addWidget(m_settings);
  layout->addLayout(toolbar);
  refreshIcons();
  if (auto *theme = m_services.get<ThemeService>()) {
    connect(theme, &ThemeService::themeChanged, this, &TodoSticker::refreshIcons);
  }

  m_dayTimer = new QTimer(this);
  m_dayTimer->setInterval(60000);
  connect(m_dayTimer, &QTimer::timeout, this,
          [this]() { m_controller->setToday(QDate::currentDate()); });

  m_lists = new QComboBox(this);
  m_lists->setObjectName(QStringLiteral("todoStickerLists"));
  m_lists->setAccessibleName(tr("TODO list"));
  m_lists->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_lists->setMinimumContentsLength(8);
  layout->addWidget(m_lists);

  m_tasks = new TodoTaskView(m_services, this);
  m_tasks->setObjectName(QStringLiteral("todoStickerTasks"));
  m_tasks->setAccessibleName(tr("TODO tasks"));
  m_tasks->setModel(m_controller->getTaskModel());
  m_tasks->setDragDropMode(QAbstractItemView::NoDragDrop);
  layout->addWidget(m_tasks, 1);

  m_message = new QLabel(this);
  m_message->setObjectName(QStringLiteral("todoStickerMessage"));
  m_message->setProperty("SettingsDescription", true);
  m_message->setTextFormat(Qt::PlainText);
  m_message->setWordWrap(true);
  m_message->setMargin(4);
  m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_message);

  connect(m_tasks, &TodoTaskView::taskActivated, this, [this](const QModelIndex &p_index) {
    if (!m_controller->isReady() || m_controller->isBusy() || !p_index.isValid()) {
      return;
    }
    const QString key = p_index.data(TodoTaskModel::IdRole).toString();
    if (!key.isEmpty()) {
      m_controller->openTask(key);
    }
  });
  connect(m_addTask, &QLineEdit::returnPressed, this, &TodoSticker::submitTask);
  connect(m_edit, &QToolButton::clicked, m_controller, &TodoStickerController::edit);
  connect(m_settings, &QToolButton::clicked, this, &TodoSticker::chooseFile);
  connect(m_lists, &QComboBox::currentIndexChanged, this, [this](int p_index) {
    if (p_index >= 0 && !m_controller->isBusy()) {
      const auto kind = m_lists->itemData(p_index, TodoListModel::ViewKindRole);
      if (kind.isValid()) {
        m_controller->setView(kind.value<TodoViewKind>(), m_lists->itemData(p_index).toString());
      }
    }
  });
  connect(m_controller, &TodoStickerController::taskAdditionFinished, this,
          [this](int p_token, bool p_created) {
            if (p_token != m_addTaskToken) {
              return;
            }
            m_addTaskToken = 0;
            const bool clearInput = p_created && m_addTask->text() == m_addTaskText;
            m_addTaskText.clear();
            if (clearInput) {
              m_addTask->clear();
            }
          });
  connect(m_controller, &TodoStickerController::stateChanged, this, &TodoSticker::updateState);
  connect(m_controller, &TodoStickerController::errorOccurred, this, &TodoSticker::updateState);

  m_hookManager = m_services.get<HookManager>();
  if (m_hookManager) {
    const QPointer<TodoSticker> self(this);
    const auto onCalendarDate = [self](HookContext &, const QVariantMap &p_args) {
      if (self) {
        self->setDueDate(
            QDate::fromString(p_args.value(QStringLiteral("date")).toString(), Qt::ISODate));
      }
    };
    m_dateChangedHookId =
        m_hookManager->addAction(HookNames::DashboardCalendarDateChanged, onCalendarDate);
    m_dateClickedHookId =
        m_hookManager->addAction(HookNames::DashboardCalendarDateClicked, onCalendarDate);
  }
  updateState();
}

TodoSticker::~TodoSticker() {
  if (m_hookManager) {
    if (m_dateChangedHookId >= 0) {
      m_hookManager->removeAction(m_dateChangedHookId);
    }
    if (m_dateClickedHookId >= 0) {
      m_hookManager->removeAction(m_dateClickedHookId);
    }
  }
}

QString TodoSticker::typeId() const { return QStringLiteral("todos"); }

QString TodoSticker::titleText() const { return tr("TODOs"); }

void TodoSticker::setDueDate(QDate p_date) { m_controller->setDueDate(p_date); }

void TodoSticker::showEvent(QShowEvent *p_event) {
  Sticker::showEvent(p_event);
  m_controller->setToday(QDate::currentDate());
  m_dayTimer->start();
  if (!m_initialized) {
    m_initialized = true;
    m_controller->initialize();
  } else {
    m_controller->refresh();
  }
}

void TodoSticker::hideEvent(QHideEvent *p_event) {
  m_dayTimer->stop();
  Sticker::hideEvent(p_event);
}

void TodoSticker::refreshIcons() {
  if (auto *theme = m_services.get<ThemeService>()) {
    const QVector<IconUtils::OverriddenColor> colors{
        {theme->paletteColor(QStringLiteral("widgets#toolbar#icon#fg")), QIcon::Normal},
        {theme->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg")), QIcon::Disabled}};
    m_edit->setIcon(
        IconUtils::fetchIcon(theme->getIconFile(QStringLiteral("edit_editor.svg")), colors));
    m_settings->setIcon(
        IconUtils::fetchIcon(theme->getIconFile(QStringLiteral("settings.svg")), colors));
  }
}

void TodoSticker::updateState() {
  const bool ready = m_controller->isReady();
  const bool busy = m_controller->isBusy();
  const auto &document = m_controller->getDocument();
  {
    const QSignalBlocker blocker(m_lists);
    const auto myDay = tr("My Day");
    const auto important = tr("Important");
    const auto planned = tr("Planned");
    const auto &dueDate = m_controller->getDueDate();
    const QString dueDateLabel =
        dueDate.isValid()
            ? tr("Due Date on %1").arg(QLocale().toString(dueDate, QLocale::ShortFormat))
            : QString();
    QHash<QString, int> titleCounts{{myDay, 1}, {important, 1}, {planned, 1}};
    if (dueDate.isValid()) {
      ++titleCounts[dueDateLabel];
    }
    for (const auto &list : document.m_lists) {
      ++titleCounts[list.m_title];
    }
    int row = 0;
    const auto appendEntry = [&](const QString &p_label, TodoViewKind p_view,
                                 const QString &p_key = QString()) {
      if (row >= m_lists->count()) {
        m_lists->addItem(p_label, p_key);
      } else {
        if (m_lists->itemText(row) != p_label) {
          m_lists->setItemText(row, p_label);
        }
        if (m_lists->itemData(row).toString() != p_key) {
          m_lists->setItemData(row, p_key);
        }
      }
      const auto kind = QVariant::fromValue(p_view);
      if (m_lists->itemData(row, TodoListModel::ViewKindRole) != kind) {
        m_lists->setItemData(row, kind, TodoListModel::ViewKindRole);
      }
      ++row;
    };
    appendEntry(myDay, TodoViewKind::MyDay);
    appendEntry(important, TodoViewKind::Important);
    appendEntry(planned, TodoViewKind::Planned);
    if (dueDate.isValid()) {
      appendEntry(dueDateLabel, TodoViewKind::All);
    }
    for (const auto &list : document.m_lists) {
      QString label = list.m_title.isEmpty() ? tr("Untitled list") : list.m_title;
      const auto *group = document.groupForList(list);
      if (group && (!group->m_isDefault || titleCounts.value(list.m_title) > 1)) {
        const QString groupTitle =
            group->m_isDefault ? tr("Default")
                               : (group->m_title.isEmpty() ? tr("Untitled group") : group->m_title);
        label = tr("%1 / %2").arg(groupTitle, label);
      }
      appendEntry(label, TodoViewKind::List, list.m_key);
    }
    while (m_lists->count() > row) {
      m_lists->removeItem(m_lists->count() - 1);
    }
    const int selected = m_controller->getViewKind() == TodoViewKind::List
                             ? (m_controller->getSelectedListKey().isEmpty()
                                    ? -1
                                    : m_lists->findData(m_controller->getSelectedListKey()))
                             : m_lists->findData(QVariant::fromValue(m_controller->getViewKind()),
                                                 TodoListModel::ViewKindRole);
    m_lists->setCurrentIndex(selected);
  }

  m_addTask->setEnabled(m_controller->canEditTasks());
  m_addTask->setReadOnly(busy);
  m_edit->setEnabled(ready && !busy);
  m_edit->setToolTip(m_controller->getFilePath().isEmpty()
                         ? tr("Edit")
                         : tr("Edit") + QLatin1Char('\n') + m_controller->getFilePath());
  m_settings->setEnabled(!busy);
  m_lists->setEnabled(ready && !busy && m_lists->count() > 0);
  m_tasks->setShowLocation(m_controller->getViewKind() != TodoViewKind::List);
  m_tasks->setEnabled(ready && !busy);
  m_tasks->setVisible(ready && m_controller->getTaskModel()->rowCount() > 0);

  QString message = m_controller->getError();
  if (message.isEmpty()) {
    if (busy) {
      message = ready ? tr("Saving TODOs...") : tr("Loading TODOs...");
    } else if (!ready) {
      message = tr("Loading TODOs...");
    } else if (document.m_lists.isEmpty()) {
      message = tr("No TODO lists.");
    } else if (m_controller->getTaskModel()->rowCount() == 0) {
      message = tr("No tasks in this list.");
    }
  }
  m_message->setText(message);
  m_message->setVisible(!message.isEmpty());
}

void TodoSticker::submitTask() {
  if (m_controller->isBusy() || !m_controller->canEditTasks()) {
    return;
  }
  const QString title = m_addTask->text();
  if (title.trimmed().isEmpty()) {
    return;
  }

  const QPointer<TodoSticker> self(this);
  const auto kind = m_controller->getViewKind();
  const QString selectedKey = m_controller->getSelectedListKey();
  const QDate selectedDate = m_controller->getDueDate();
  const int listCount = m_controller->getDocument().m_lists.size();
  QString destination;
  if (kind == TodoViewKind::List) {
    destination = selectedKey;
  } else if (listCount == 1) {
    destination = m_controller->getDocument().m_lists.first().m_key;
  }
  QDate due = kind == TodoViewKind::All ? selectedDate : QDate();
  const bool chooseDestination = kind != TodoViewKind::List && listCount > 1;
  if (chooseDestination || kind == TodoViewKind::Planned) {
    const QString path = m_controller->getFilePath();
    const QString source = m_controller->getDocument().m_source;
    // Keep the modal outside the sticker's shortcut and ownership hierarchy.
    QDialog dialog;
    dialog.setObjectName(QStringLiteral("todoStickerAddTaskDialog"));
    dialog.setWindowTitle(tr("Add Task"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    layout->addLayout(form);
    QComboBox *destinations = nullptr;
    if (chooseDestination) {
      destinations = new QComboBox(&dialog);
      destinations->setObjectName(QStringLiteral("todoStickerTaskDestination"));
      destinations->setAccessibleName(tr("List"));
      for (int row = 0; row < m_lists->count(); ++row) {
        if (m_lists->itemData(row, TodoListModel::ViewKindRole).value<TodoViewKind>() ==
            TodoViewKind::List) {
          destinations->addItem(m_lists->itemText(row), m_lists->itemData(row));
        }
      }
      form->addRow(tr("List"), destinations);
    }
    QDateEdit *dueDate = nullptr;
    if (kind == TodoViewKind::Planned) {
      dueDate = new QDateEdit(QDate::currentDate(), &dialog);
      dueDate->setObjectName(QStringLiteral("todoStickerDueDate"));
      dueDate->setAccessibleName(tr("Due date"));
      dueDate->setCalendarPopup(true);
      dueDate->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
      form->addRow(tr("Due date"), dueDate);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(this, &QObject::destroyed, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !self) {
      return;
    }
    if (m_controller->isBusy() || !m_controller->canEditTasks() ||
        m_controller->getFilePath() != path || m_controller->getDocument().m_source != source ||
        m_controller->getViewKind() != kind || m_controller->getSelectedListKey() != selectedKey ||
        m_controller->getDueDate() != selectedDate) {
      return;
    }
    if (destinations) {
      destination = destinations->currentData().toString();
    }
    if (dueDate) {
      due = dueDate->date();
    }
  }

  // Admission follows controller state, not a possibly cancelled binding's old token.
  const int token = m_controller->addTask(title, destination, due);
  if (self) {
    m_addTaskToken = token;
    m_addTaskText = token ? title : QString();
  }
}

void TodoSticker::chooseFile() {
  if (m_controller->isBusy()) {
    return;
  }
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Choose TODO File"), m_controller->getFilePath(), tr("TODO files (*.todo)"));
  if (!path.isEmpty()) {
    m_controller->setFilePath(path);
  }
}
