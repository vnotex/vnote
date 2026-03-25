#include "settingspagehelper.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QVBoxLayout>

#include <widgets/propertydefs.h>

using namespace vnotex;

QFrame *SettingsPageHelper::createCard(QWidget *p_parent) {
  auto *card = new QFrame(p_parent);
  card->setProperty(PropertyDefs::c_settingsCard, true);

  auto *layout = new QVBoxLayout(card);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  return card;
}

QLabel *SettingsPageHelper::createCardTitle(const QString &p_title, QWidget *p_parent) {
  auto *label = new QLabel(p_title, p_parent);
  label->setProperty(PropertyDefs::c_settingsCardTitle, true);
  return label;
}

QLabel *SettingsPageHelper::createDescription(const QString &p_text, QWidget *p_parent) {
  auto *label = new QLabel(p_text, p_parent);
  label->setProperty(PropertyDefs::c_settingsDescription, true);
  label->setWordWrap(true);
  return label;
}

QFrame *SettingsPageHelper::createSeparator(QWidget *p_parent) {
  auto *sep = new QFrame(p_parent);
  sep->setProperty(PropertyDefs::c_settingsSeparator, true);
  sep->setFrameShape(QFrame::HLine);
  sep->setFrameShadow(QFrame::Plain);
  sep->setFixedHeight(1);
  return sep;
}

QWidget *SettingsPageHelper::createSettingRow(const QString &p_label, const QString &p_description,
                                             QWidget *p_control, QWidget *p_parent) {
  auto *row = new QWidget(p_parent);
  row->setProperty(PropertyDefs::c_settingsRow, true);

  // Use vertical layout when the control is or contains a text input
  // (QLineEdit, QPlainTextEdit, QTextEdit) so the edit gets the full row width
  // instead of being squeezed into the right half.
  bool useVertical = false;
  if (p_control) {
    useVertical = qobject_cast<QLineEdit *>(p_control) || qobject_cast<QPlainTextEdit *>(p_control)
                  || qobject_cast<QTextEdit *>(p_control) || p_control->findChild<QLineEdit *>()
                  || p_control->findChild<QPlainTextEdit *>() || p_control->findChild<QTextEdit *>();
  }

  if (useVertical) {
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(16, 6, 16, 6);
    rowLayout->setSpacing(2);

    auto *nameLabel = new QLabel(p_label, row);
    rowLayout->addWidget(nameLabel);

    if (!p_description.isEmpty()) {
      auto *descLabel = createDescription(p_description, row);
      rowLayout->addWidget(descLabel);
    }

    p_control->setParent(row);
    rowLayout->addWidget(p_control);
  } else {
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(16, 6, 16, 6);
    rowLayout->setSpacing(12);

    // Left side: label + optional description stacked vertically.
    auto *leftLayout = new QVBoxLayout();
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);

    auto *nameLabel = new QLabel(p_label, row);
    leftLayout->addWidget(nameLabel);

    if (!p_description.isEmpty()) {
      auto *descLabel = createDescription(p_description, row);
      leftLayout->addWidget(descLabel);
    }

    rowLayout->addLayout(leftLayout, 1);

    // Right side: control widget.
    if (p_control) {
      p_control->setParent(row);
      rowLayout->addWidget(p_control, 0, Qt::AlignRight | Qt::AlignVCenter);
    }
  }

  return row;
}

QWidget *SettingsPageHelper::createCheckBoxRow(QCheckBox *p_checkBox, const QString &p_description,
                                              QWidget *p_parent) {
  auto *row = new QWidget(p_parent);
  row->setProperty(PropertyDefs::c_settingsRow, true);

  auto *rowLayout = new QVBoxLayout(row);
  rowLayout->setContentsMargins(16, 6, 16, 6);
  rowLayout->setSpacing(2);

  if (p_checkBox) {
    p_checkBox->setParent(row);
    rowLayout->addWidget(p_checkBox);
  }

  if (!p_description.isEmpty()) {
    auto *descLabel = createDescription(p_description, row);
    descLabel->setContentsMargins(0, 0, 0, 0);
    rowLayout->addWidget(descLabel);
  }

  return row;
}

QVBoxLayout *SettingsPageHelper::addSection(QVBoxLayout *p_parentLayout, const QString &p_title,
                                            const QString &p_description, QWidget *p_parent) {
  auto *card = createCard(p_parent);
  auto *cardLayout = qobject_cast<QVBoxLayout *>(card->layout());
  Q_ASSERT(cardLayout);

  // Title row with padding.
  auto *titleWidget = new QWidget(card);
  auto *titleLayout = new QVBoxLayout(titleWidget);
  titleLayout->setContentsMargins(16, 12, 16, 8);
  titleLayout->setSpacing(4);
  titleLayout->addWidget(createCardTitle(p_title, titleWidget));
  if (!p_description.isEmpty()) {
    titleLayout->addWidget(createDescription(p_description, titleWidget));
  }
  cardLayout->addWidget(titleWidget);

  // Separator after title.
  cardLayout->addWidget(createSeparator(card));

  p_parentLayout->addWidget(card);

  return cardLayout;
}
