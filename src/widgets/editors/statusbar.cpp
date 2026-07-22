#include "statusbar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>

using namespace vnotex;

StatusBar::StatusBar(const StatusBarDef &p_def, QWidget *p_parent) : QWidget(p_parent) {
  m_layout = new QHBoxLayout(this);
  m_layout->setContentsMargins(0, 0, 0, 0);
  m_layout->setSpacing(0);

  for (const auto &column : p_def) {
    buildColumn(column);
  }

  // Empty def => nothing to show. Keep the bar hidden so it takes no space.
  if (m_columns.isEmpty()) {
    hide();
  }
}

void StatusBar::buildColumn(const StatusBarColumn &p_column) {
  const int idx = m_columns.size();
  StatusBarColumn column = p_column;

  QWidget *widget = nullptr;

  switch (column.type) {
  case StatusBarColumnType::Label: {
    auto *label = new QLabel(column.text, this);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    widget = label;
    break;
  }

  case StatusBarColumnType::Button: {
    auto *button = new QToolButton(this);
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
    auto *button = new QToolButton(this);
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
    auto *edit = new QLineEdit(column.text, this);
    edit->setPlaceholderText(column.placeholder);
    if (column.onTextEdited) {
      auto callback = column.onTextEdited;
      connect(edit, &QLineEdit::textEdited, this,
              [callback](const QString &p_text) { callback(p_text); });
    }
    widget = edit;
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

  const QStringList actions = m_columns[p_index].menuActions;
  auto onTriggered = m_columns[p_index].onTriggered;
  for (int i = 0; i < actions.size(); ++i) {
    QAction *action = menu->addAction(actions.at(i));
    if (onTriggered) {
      connect(action, &QAction::triggered, this, [onTriggered, i]() { onTriggered(i); });
    }
  }
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

void StatusBar::setColumnMenuActions(int p_index, const QStringList &p_actions) {
  if (!isValidIndex(p_index)) {
    return;
  }
  if (m_columns[p_index].type != StatusBarColumnType::Menu) {
    return;
  }

  m_columns[p_index].menuActions = p_actions;
  rebuildMenu(p_index);
}

void StatusBar::setColumnEditText(int p_index, const QString &p_text) {
  if (!isValidIndex(p_index)) {
    return;
  }
  if (m_columns[p_index].type != StatusBarColumnType::Edit) {
    return;
  }

  m_columns[p_index].text = p_text;
  if (auto *edit = qobject_cast<QLineEdit *>(m_widgets[p_index])) {
    edit->setText(p_text);
  }
}
