#include "settingspagehelper.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

#include <widgets/propertydefs.h>

using namespace vnotex;

namespace {
class SettingLabel : public QLabel {
public:
  SettingLabel(const QString &p_text, bool p_compact, QWidget *p_parent)
      : QLabel(p_text, p_parent), m_compact(p_compact) {
    setWordWrap(true);
    setTextFormat(Qt::PlainText);
  }

  QSize sizeHint() const override {
    if (!m_compact) {
      return QLabel::sizeHint();
    }
    // A preferred column, not a fixed/minimum width: the scroll area must still
    // be able to shrink the page and let QFormLayout wrap a long pair.
    const int width = fontMetrics().horizontalAdvance(QLatin1Char('x')) * 24;
    return QSize(width, heightForWidth(width));
  }

protected:
  void mouseReleaseEvent(QMouseEvent *p_event) override {
    if (p_event->button() == Qt::LeftButton && rect().contains(p_event->position().toPoint()) &&
        buddy() && buddy()->isEnabled()) {
      buddy()->setFocus(Qt::MouseFocusReason);
      p_event->accept();
      return;
    }
    QLabel::mouseReleaseEvent(p_event);
  }

private:
  const bool m_compact;
};
} // namespace

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
                                              QWidget *p_control, QWidget *p_parent,
                                              RowLayout p_layout, QWidget *p_focusControl) {
  auto *row = new QWidget(p_parent);
  row->setProperty(PropertyDefs::c_settingsRow, true);
  auto *rowLayout = new QVBoxLayout(row);
  rowLayout->setContentsMargins(16, 6, 16, 6);
  rowLayout->setSpacing(4);

  auto *nameLabel = new SettingLabel(p_label, p_layout == RowLayout::Compact, row);
  auto *focusControl = p_focusControl ? p_focusControl : p_control;
  if (focusControl) {
    nameLabel->setBuddy(focusControl);
    if (focusControl->accessibleName().isEmpty()) {
      focusControl->setAccessibleName(p_label);
    }
    if (focusControl->accessibleDescription().isEmpty()) {
      focusControl->setAccessibleDescription(p_description);
    }
  }

  if (p_layout == RowLayout::FullWidth || !p_control) {
    rowLayout->addWidget(nameLabel);
    if (p_control) {
      rowLayout->addWidget(p_control);
    }
  } else {
    // Widget type only controls sizing, never label placement. In particular, a
    // spin box's internal line edit must not turn an ordinary value into a text area.
    if (qobject_cast<QComboBox *>(p_control) || qobject_cast<QAbstractSpinBox *>(p_control)) {
      auto policy = p_control->sizePolicy();
      policy.setHorizontalPolicy(QSizePolicy::Preferred);
      p_control->setSizePolicy(policy);
    }
    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(4);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(nameLabel, p_control);
    rowLayout->addLayout(form);
  }

  if (!p_description.isEmpty()) {
    rowLayout->addWidget(createDescription(p_description, row));
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
  titleWidget->setProperty(PropertyDefs::c_settingsContainer, true);
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
