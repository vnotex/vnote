#include "widgetsfactory.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QToolButton>

#include "combobox.h"
#include "lineeditwithsnippet.h"

using namespace vnotex;

QMenu *WidgetsFactory::createMenu(QWidget *p_parent) {
  auto menu = new QMenu(p_parent);
  menu->setToolTipsVisible(true);
  return menu;
}

QMenu *WidgetsFactory::createMenu(const QString &p_title, QWidget *p_parent) {
  auto menu = new QMenu(p_title, p_parent);
  menu->setToolTipsVisible(true);
  return menu;
}

QLineEdit *WidgetsFactory::createLineEdit(QWidget *p_parent) { return new LineEdit(p_parent); }

QLineEdit *WidgetsFactory::createLineEdit(const QString &p_contents, QWidget *p_parent) {
  return new LineEdit(p_contents, p_parent);
}

LineEditWithSnippet *WidgetsFactory::createLineEditWithSnippet(QWidget *p_parent) {
  return new LineEditWithSnippet(p_parent);
}

LineEditWithSnippet *WidgetsFactory::createLineEditWithSnippet(const QString &p_contents,
                                                               QWidget *p_parent) {
  return new LineEditWithSnippet(p_contents, p_parent);
}

QComboBox *WidgetsFactory::createComboBox(QWidget *p_parent) {
  auto comboBox = new ComboBox(p_parent);
  auto itemDelegate = new QStyledItemDelegate(comboBox);
  comboBox->setItemDelegate(itemDelegate);
  return comboBox;
}

QCheckBox *WidgetsFactory::createCheckBox(const QString &p_text, QWidget *p_parent) {
  return new QCheckBox(p_text, p_parent);
}

QRadioButton *WidgetsFactory::createRadioButton(const QString &p_text, QWidget *p_parent) {
  return new QRadioButton(p_text, p_parent);
}

QSpinBox *WidgetsFactory::createSpinBox(QWidget *p_parent) { return new QSpinBox(p_parent); }

QDoubleSpinBox *WidgetsFactory::createDoubleSpinBox(QWidget *p_parent) {
  return new QDoubleSpinBox(p_parent);
}

QToolButton *WidgetsFactory::createToolButton(QWidget *p_parent) {
  auto tb = new QToolButton(p_parent);
  tb->setPopupMode(QToolButton::MenuButtonPopup);
  return tb;
}

QFormLayout *WidgetsFactory::createFormLayout(QWidget *p_parent) {
  auto layout = new QFormLayout(p_parent);

#if defined(Q_OS_MACOS)
  layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
#endif

  return layout;
}

QPlainTextEdit *WidgetsFactory::createPlainTextConsole(QWidget *p_parent) {
  auto edit = new QPlainTextEdit(p_parent);
  edit->setProperty("ConsoleTextEdit", true);
  edit->setReadOnly(true);
  edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  return edit;
}

QPlainTextEdit *WidgetsFactory::createPlainTextEdit(QWidget *p_parent) {
  auto edit = new QPlainTextEdit(p_parent);
  edit->setProperty("ConsoleTextEdit", true);
  return edit;
}
