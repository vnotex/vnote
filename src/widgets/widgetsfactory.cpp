#include "widgetsfactory.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QUrl>

#include <core/services/snippetcoreservice.h>

#include "combobox.h"
#include "propertydefs.h"
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

QLineEdit *WidgetsFactory::createUrlUserNameEdit(QLineEdit *p_urlEdit, QWidget *p_parent) {
  auto *edit = createLineEdit(p_parent);
  const auto loadUsername = [edit](const QString &p_text) {
    const QUrl url(p_text.trimmed());
    const QSignalBlocker blocker(edit);
    edit->setText(url.userName());
    edit->setEnabled(url.scheme() == QLatin1String("https"));
  };
  QObject::connect(p_urlEdit, &QLineEdit::textChanged, edit, loadUsername);
  QObject::connect(edit, &QLineEdit::textEdited, p_urlEdit, [p_urlEdit](const QString &p_username) {
    QUrl url(p_urlEdit->text().trimmed());
    if (url.scheme() != QLatin1String("https")) {
      return;
    }
    const QString username = p_username.trimmed();
    url.setUserName(username.isEmpty() ? QString() : username);
    p_urlEdit->setText(url.toString(QUrl::FullyEncoded));
  });
  loadUsername(p_urlEdit->text());
  return edit;
}

LineEditWithSnippet *WidgetsFactory::createLineEditWithSnippet(
    SnippetCoreService *p_snippetService, QWidget *p_parent) {
  return new LineEditWithSnippet(p_snippetService, p_parent);
}

LineEditWithSnippet *WidgetsFactory::createLineEditWithSnippet(
    SnippetCoreService *p_snippetService, const QString &p_contents,
                                                                QWidget *p_parent) {
  return new LineEditWithSnippet(p_snippetService, p_contents, p_parent);
}

QComboBox *WidgetsFactory::createComboBox(QWidget *p_parent) {
  auto comboBox = new ComboBox(p_parent);
  auto itemDelegate = new QStyledItemDelegate(comboBox);
  comboBox->setItemDelegate(itemDelegate);
  return comboBox;
}

QComboBox *WidgetsFactory::createEditableComboBox(QWidget *p_parent) {
  auto comboBox = createComboBox(p_parent);
  comboBox->setEditable(true);
  comboBox->setLineEdit(createLineEdit(p_parent));
  comboBox->lineEdit()->setProperty(PropertyDefs::c_embeddedLineEdit, true);
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
