#include "statusbar.h"

#include <QActionGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>

using namespace vnotex;

StatusBar::StatusBar(const StatusBarDef &p_def, QWidget *p_parent) : QWidget(p_parent) {
  m_stackLayout = new QStackedLayout(this);
  m_stackLayout->setContentsMargins(0, 0, 0, 0);
  m_stackLayout->setStackingMode(QStackedLayout::StackOne);

  // Page 0: columns host.
  m_columnsHost = new QWidget(this);
  m_layout = new QHBoxLayout(m_columnsHost);
  m_layout->setContentsMargins(0, 0, 0, 0);
  m_layout->setSpacing(0);
  m_stackLayout->addWidget(m_columnsHost);

  for (const auto &column : p_def) {
    buildColumn(column);
  }

  // Page 1: transient message label. Created after the columns so column
  // widgets keep their leading position in child-order lookups.
  m_messageLabel = new QLabel(this);
  m_messageLabel->setObjectName(QStringLiteral("StatusBarMessageLabel"));
  m_messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_stackLayout->addWidget(m_messageLabel);

  m_stackLayout->setCurrentWidget(m_columnsHost);

  m_messageTimer = new QTimer(this);
  m_messageTimer->setSingleShot(true);
  connect(m_messageTimer, &QTimer::timeout, this, &StatusBar::clearMessage);

  // Empty def => nothing to show. Keep the bar hidden so it takes no space.
  if (m_columns.isEmpty()) {
    hide();
  }
}

StatusBar::~StatusBar() {
  // Detach shared-owned widgets so their real owner (e.g. the editor's input
  // mode) is not left with a dangling child pointer.
  for (auto it = m_widgetColumnShared.begin(); it != m_widgetColumnShared.end(); ++it) {
    if (it.value()) {
      it.value()->setParent(nullptr);
    }
  }
}

void StatusBar::buildColumn(const StatusBarColumn &p_column) {
  const int idx = m_columns.size();
  StatusBarColumn column = p_column;

  QWidget *widget = nullptr;

  switch (column.type) {
  case StatusBarColumnType::Label: {
    auto *label = new QLabel(column.text, m_columnsHost);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    widget = label;
    break;
  }

  case StatusBarColumnType::Button: {
    auto *button = new QToolButton(m_columnsHost);
    button->setText(column.text);
    if (!column.icon.isNull()) {
      button->setIcon(column.icon);
    }
    if (column.onClicked) {
      auto callback = column.onClicked;
      connect(button, &QToolButton::clicked, this, [callback]() { callback(); });
    }
    widget = button;
    break;
  }

  case StatusBarColumnType::Menu: {
    auto *button = new QToolButton(m_columnsHost);
    button->setText(column.text);
    if (!column.icon.isNull()) {
      button->setIcon(column.icon);
    }
    button->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(button);
    button->setMenu(menu);
    widget = button;
    break;
  }

  case StatusBarColumnType::Edit: {
    auto *edit = new QLineEdit(column.text, m_columnsHost);
    edit->setPlaceholderText(column.placeholder);
    if (column.onTextEdited) {
      auto callback = column.onTextEdited;
      connect(edit, &QLineEdit::textEdited, this,
              [callback](const QString &p_text) { callback(p_text); });
    }
    widget = edit;
    break;
  }

  case StatusBarColumnType::Widget: {
    // Empty mount; a widget is inserted later via setColumnWidget.
    auto *mount = new QWidget(m_columnsHost);
    auto *mountLayout = new QHBoxLayout(mount);
    mountLayout->setContentsMargins(0, 0, 0, 0);
    mountLayout->setSpacing(0);
    widget = mount;
    break;
  }

  case StatusBarColumnType::Spacer:
    // No persistent widget; a stretch handles the spacing.
    break;
  }

  m_columns.append(column);
  m_widgets.append(widget);

  if (column.type == StatusBarColumnType::Spacer) {
    m_layout->addStretch(column.stretch > 0 ? column.stretch : 1);
    return;
  }

  if (widget) {
    if (!column.style.isEmpty()) {
      widget->setStyleSheet(column.style);
    }
    widget->setVisible(column.visible);
    m_layout->addWidget(widget, 0);
  }

  // Menu columns need their action list built after the widget is stored so
  // rebuildMenu can find it by index.
  if (column.type == StatusBarColumnType::Menu) {
    rebuildMenu(idx);
  }
}

void StatusBar::rebuildMenu(int p_index) {
  if (!isValidIndex(p_index)) {
    return;
  }
  if (m_columns[p_index].type != StatusBarColumnType::Menu) {
    return;
  }

  auto *button = qobject_cast<QToolButton *>(m_widgets[p_index]);
  if (!button || !button->menu()) {
    return;
  }

  QMenu *menu = button->menu();
  menu->clear();

  const auto &items = m_columns[p_index].menuItems;
  auto onMenuTriggered = m_columns[p_index].onMenuTriggered;
  QHash<int, QActionGroup *> groups;
  for (int i = 0; i < items.size(); ++i) {
    const StatusBarMenuItem &item = items.at(i);
    if (item.separator) {
      menu->addSeparator();
      continue;
    }

    QAction *action = menu->addAction(item.text);
    if (item.checkable) {
      action->setCheckable(true);
      action->setChecked(item.checked);
    }
    if (item.exclusiveGroupId >= 0) {
      QActionGroup *&group = groups[item.exclusiveGroupId];
      if (!group) {
        group = new QActionGroup(menu);
      }
      group->addAction(action);
    }
    if (onMenuTriggered) {
      connect(action, &QAction::triggered, this,
              [onMenuTriggered, i](bool p_checked) { onMenuTriggered(i, p_checked); });
    }
  }
}

QHBoxLayout *StatusBar::widgetColumnMountLayout(int p_index) const {
  if (!isValidIndex(p_index) || m_columns[p_index].type != StatusBarColumnType::Widget) {
    return nullptr;
  }
  QWidget *mount = m_widgets[p_index];
  return mount ? qobject_cast<QHBoxLayout *>(mount->layout()) : nullptr;
}

void StatusBar::detachColumnWidget(int p_index) {
  auto *mountLayout = widgetColumnMountLayout(p_index);
  if (!mountLayout) {
    return;
  }

  // Remove any currently hosted widget from the mount.
  while (QLayoutItem *item = mountLayout->takeAt(0)) {
    if (QWidget *w = item->widget()) {
      w->hide();
      w->setParent(nullptr);
    }
    delete item;
  }

  // Release shared ownership (if any).
  m_widgetColumnShared.remove(p_index);
}

void StatusBar::setColumnWidget(int p_index, const QSharedPointer<QWidget> &p_widget) {
  auto *mountLayout = widgetColumnMountLayout(p_index);
  if (!mountLayout) {
    return;
  }

  detachColumnWidget(p_index);

  QWidget *mount = m_widgets[p_index];
  if (p_widget) {
    m_widgetColumnShared.insert(p_index, p_widget);
    p_widget->setParent(mount);
    mountLayout->addWidget(p_widget.data());
    p_widget->show();
    mount->setVisible(m_columns[p_index].visible);
  } else {
    mount->hide();
  }
}

void StatusBar::setColumnWidget(int p_index, QWidget *p_widget) {
  auto *mountLayout = widgetColumnMountLayout(p_index);
  if (!mountLayout) {
    return;
  }

  detachColumnWidget(p_index);

  QWidget *mount = m_widgets[p_index];
  if (p_widget) {
    p_widget->setParent(mount);
    mountLayout->addWidget(p_widget);
    p_widget->show();
    mount->setVisible(m_columns[p_index].visible);
  } else {
    mount->hide();
  }
}

void StatusBar::showMessage(const QString &p_msg, int p_milliseconds) {
  if (p_msg.isEmpty()) {
    clearMessage();
    return;
  }

  m_messageLabel->setText(p_msg);
  m_stackLayout->setCurrentWidget(m_messageLabel);

  if (p_milliseconds > 0) {
    m_messageTimer->start(p_milliseconds);
  }
}

void StatusBar::clearMessage() {
  m_messageLabel->clear();
  m_stackLayout->setCurrentWidget(m_columnsHost);
}

bool StatusBar::isValidIndex(int p_index) const {
  return p_index >= 0 && p_index < m_columns.size();
}

void StatusBar::setColumnText(int p_index, const QString &p_text) {
  if (!isValidIndex(p_index)) {
    return;
  }

  const StatusBarColumnType type = m_columns[p_index].type;
  if (type != StatusBarColumnType::Label && type != StatusBarColumnType::Button &&
      type != StatusBarColumnType::Edit) {
    return;
  }

  m_columns[p_index].text = p_text;

  QWidget *widget = m_widgets[p_index];
  if (auto *label = qobject_cast<QLabel *>(widget)) {
    label->setText(p_text);
  } else if (auto *button = qobject_cast<QToolButton *>(widget)) {
    button->setText(p_text);
  } else if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
    edit->setText(p_text);
  }
}

void StatusBar::setColumnVisible(int p_index, bool p_visible) {
  if (!isValidIndex(p_index)) {
    return;
  }

  m_columns[p_index].visible = p_visible;
  if (m_widgets[p_index]) {
    m_widgets[p_index]->setVisible(p_visible);
  }
}

void StatusBar::setColumnStyle(int p_index, const QString &p_qss) {
  if (!isValidIndex(p_index)) {
    return;
  }

  m_columns[p_index].style = p_qss;
  if (m_widgets[p_index]) {
    m_widgets[p_index]->setStyleSheet(p_qss);
  }
}

void StatusBar::setColumnIcon(int p_index, const QIcon &p_icon) {
  if (!isValidIndex(p_index)) {
    return;
  }

  const StatusBarColumnType type = m_columns[p_index].type;
  if (type != StatusBarColumnType::Button && type != StatusBarColumnType::Menu) {
    return;
  }

  m_columns[p_index].icon = p_icon;
  if (auto *button = qobject_cast<QToolButton *>(m_widgets[p_index])) {
    button->setIcon(p_icon);
  }
}

void StatusBar::setColumnMenuItems(int p_index, const QVector<StatusBarMenuItem> &p_items) {
  if (!isValidIndex(p_index)) {
    return;
  }
  if (m_columns[p_index].type != StatusBarColumnType::Menu) {
    return;
  }

  m_columns[p_index].menuItems = p_items;
  rebuildMenu(p_index);
}
