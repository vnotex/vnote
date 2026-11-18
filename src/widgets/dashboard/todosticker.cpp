#include "todosticker.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHash>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <controllers/todostickercontroller.h>
#include <core/servicelocator.h>
#include <core/tododocument.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <models/todomodels.h>

using namespace vnotex;

namespace {

class TodoStickerTaskDelegate final : public QStyledItemDelegate {
public:
  explicit TodoStickerTaskDelegate(QObject *p_parent) : QStyledItemDelegate(p_parent) {}

  QRect checkRect(const QModelIndex &p_index, QStyleOptionViewItem p_option) const {
    initStyleOption(&p_option, p_index);
    if (!(p_option.features & QStyleOptionViewItem::HasCheckIndicator)) {
      return {};
    }
    return p_option.widget->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,
                                                    &p_option, p_option.widget);
  }

  bool editorEvent(QEvent *, QAbstractItemModel *, const QStyleOptionViewItem &,
                   const QModelIndex &) override {
    // The view separates checkbox gestures from navigation and owns the mutation boundary.
    return false;
  }
};

class TodoStickerTaskView final : public QListView {
public:
  explicit TodoStickerTaskView(TodoStickerController &p_controller, QWidget *p_parent)
      : QListView(p_parent), m_controller(p_controller) {
    m_delegate = new TodoStickerTaskDelegate(this);
    setItemDelegate(m_delegate);
  }

protected:
  void mousePressEvent(QMouseEvent *p_event) override {
    m_pressed = QPersistentModelIndex();
    m_checkboxPress = false;
    if (p_event->button() == Qt::LeftButton) {
      m_pressed = indexAt(p_event->position().toPoint());
      m_checkboxPress = isCheckHit(m_pressed, p_event->position().toPoint());
      if (m_checkboxPress) {
        setCurrentIndex(m_pressed);
        p_event->accept();
        return;
      }
    }
    QListView::mousePressEvent(p_event);
  }

  void mouseReleaseEvent(QMouseEvent *p_event) override {
    if (p_event->button() != Qt::LeftButton) {
      QListView::mouseReleaseEvent(p_event);
      return;
    }
    const QPersistentModelIndex pressed = m_pressed;
    const bool checkboxPress = m_checkboxPress;
    m_pressed = QPersistentModelIndex();
    m_checkboxPress = false;
    const auto index = indexAt(p_event->position().toPoint());
    const bool sameTask = pressed.isValid() && pressed == index;
    const bool checkboxRelease = isCheckHit(index, p_event->position().toPoint());
    if (checkboxPress || checkboxRelease) {
      p_event->accept();
      if (sameTask && checkboxPress && checkboxRelease) {
        toggleTask(index);
      }
      // setData() can reset the model synchronously. Do not inspect index afterward.
      return;
    }
    QListView::mouseReleaseEvent(p_event);
    if (sameTask && pressed.isValid()) {
      openTask(pressed);
    }
  }

  void mouseDoubleClickEvent(QMouseEvent *p_event) override {
    // The first release already handled this gesture; never toggle/open twice.
    m_pressed = QPersistentModelIndex();
    m_checkboxPress = false;
    if (p_event->button() == Qt::LeftButton) {
      p_event->accept();
      return;
    }
    QListView::mouseDoubleClickEvent(p_event);
  }

  void keyPressEvent(QKeyEvent *p_event) override {
    if (p_event->modifiers() == Qt::NoModifier &&
        (p_event->key() == Qt::Key_Return || p_event->key() == Qt::Key_Enter ||
         p_event->key() == Qt::Key_Space)) {
      p_event->accept();
      if (!p_event->isAutoRepeat()) {
        if (p_event->key() == Qt::Key_Space) {
          toggleTask(currentIndex());
        } else {
          openTask(currentIndex());
        }
      }
      return;
    }
    QListView::keyPressEvent(p_event);
  }

private:
  bool isCheckHit(const QModelIndex &p_index, const QPoint &p_position) const {
    if (!p_index.isValid()) {
      return false;
    }
    QStyleOptionViewItem option;
    initViewItemOption(&option);
    option.rect = visualRect(p_index);
    option.widget = this;
    // Let the active style place the indicator, including right-to-left layouts.
    return m_delegate->checkRect(p_index, option).contains(p_position);
  }

  void toggleTask(const QModelIndex &p_index) {
    if (!m_controller.isReady() || m_controller.isBusy() || !p_index.isValid() ||
        !(p_index.flags() & Qt::ItemIsUserCheckable)) {
      return;
    }
    const int state =
        p_index.data(Qt::CheckStateRole).toInt() == Qt::Checked ? Qt::Unchecked : Qt::Checked;
    model()->setData(p_index, state, Qt::CheckStateRole);
  }

  void openTask(const QModelIndex &p_index) {
    if (!m_controller.isReady() || m_controller.isBusy() || !p_index.isValid()) {
      return;
    }
    const QString key = p_index.data(TodoTaskModel::IdRole).toString();
    if (!key.isEmpty()) {
      m_controller.openTask(key);
    }
  }

  TodoStickerController &m_controller;
  TodoStickerTaskDelegate *m_delegate = nullptr;
  QPersistentModelIndex m_pressed;
  bool m_checkboxPress = false;
};

} // namespace

TodoSticker::TodoSticker(ServiceLocator &p_services, QWidget *p_parent)
    : Sticker(p_services, p_parent) {
  m_controller = new TodoStickerController(m_services, this);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  auto *toolbar = new QHBoxLayout();
  toolbar->setContentsMargins(4, 2, 4, 2);
  toolbar->addStretch();
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

  m_tasks = new TodoStickerTaskView(*m_controller, this);
  m_tasks->setObjectName(QStringLiteral("todoStickerTasks"));
  m_tasks->setAccessibleName(tr("TODO tasks"));
  m_tasks->setModel(m_controller->getTaskModel());
  m_tasks->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tasks->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_tasks->setDragEnabled(false);
  m_tasks->setAcceptDrops(false);
  layout->addWidget(m_tasks, 1);

  m_message = new QLabel(this);
  m_message->setObjectName(QStringLiteral("todoStickerMessage"));
  m_message->setProperty("SettingsDescription", true);
  m_message->setTextFormat(Qt::PlainText);
  m_message->setWordWrap(true);
  m_message->setMargin(4);
  m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_message);

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
  connect(m_controller, &TodoStickerController::stateChanged, this, &TodoSticker::updateState);
  connect(m_controller, &TodoStickerController::errorOccurred, this, &TodoSticker::updateState);
  updateState();
}

QString TodoSticker::typeId() const { return QStringLiteral("todos"); }

QString TodoSticker::titleText() const { return tr("TODOs"); }

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
    QHash<QString, int> titleCounts{{myDay, 1}, {important, 1}, {planned, 1}};
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

  m_edit->setEnabled(ready && !busy);
  m_edit->setToolTip(m_controller->getFilePath().isEmpty()
                         ? tr("Edit")
                         : tr("Edit") + QLatin1Char('\n') + m_controller->getFilePath());
  m_settings->setEnabled(!busy);
  m_lists->setEnabled(ready && !busy && m_lists->count() > 0);
  m_tasks->setEnabled(ready && !busy);
  m_tasks->setVisible(ready && m_controller->getTaskModel()->rowCount() > 0);

  QString message = m_controller->getError();
  if (message.isEmpty()) {
    if (busy) {
      message = ready ? tr("Saving TODOs...") : tr("Loading TODOs...");
    } else if (!ready) {
      message = tr("Loading TODOs...");
    } else if (document.m_lists.isEmpty()) {
      message = tr("No TODO lists. Use Edit to add a list.");
    } else if (m_controller->getTaskModel()->rowCount() == 0) {
      message = tr("No tasks in this list.");
    }
  }
  m_message->setText(message);
  m_message->setVisible(!message.isEmpty());
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
