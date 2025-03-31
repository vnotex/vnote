#include "snippetinfowidget2.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpressionValidator>

#include <utils/pathutils.h>
#include <utils/utils.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

SnippetInfoWidget2::SnippetInfoWidget2(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services), m_editMode(false) {
  setupUI();
}

SnippetInfoWidget2::SnippetInfoWidget2(ServiceLocator &p_services, const QJsonObject &p_snippetData,
                                       QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services), m_editMode(true) {
  setupUI();

  // Pre-fill fields from snippet data.
  if (p_snippetData.contains(QStringLiteral("name"))) {
    m_nameLineEdit->setText(p_snippetData.value(QStringLiteral("name")).toString());
  }
  if (p_snippetData.contains(QStringLiteral("description"))) {
    m_descriptionLineEdit->setText(p_snippetData.value(QStringLiteral("description")).toString());
  }
  if (p_snippetData.contains(QStringLiteral("type"))) {
    const auto type = p_snippetData.value(QStringLiteral("type")).toString();
    int idx = m_typeComboBox->findData(type);
    if (idx >= 0) {
      m_typeComboBox->setCurrentIndex(idx);
    }
  }
  if (p_snippetData.contains(QStringLiteral("cursorMark"))) {
    m_cursorMarkLineEdit->setText(p_snippetData.value(QStringLiteral("cursorMark")).toString());
  }
  if (p_snippetData.contains(QStringLiteral("selectionMark"))) {
    m_selectionMarkLineEdit->setText(
        p_snippetData.value(QStringLiteral("selectionMark")).toString());
  }
  if (p_snippetData.contains(QStringLiteral("indentAsFirstLine"))) {
    m_indentAsFirstLineCheckBox->setChecked(
        p_snippetData.value(QStringLiteral("indentAsFirstLine")).toBool());
  }
  if (p_snippetData.contains(QStringLiteral("content"))) {
    m_contentTextEdit->setPlainText(p_snippetData.value(QStringLiteral("content")).toString());
  }

  // Built-in snippets are read-only.
  if (p_snippetData.value(QStringLiteral("isBuiltin")).toBool()) {
    setReadOnly(true);
  }
}

void SnippetInfoWidget2::setupUI() {
  auto mainLayout = WidgetsFactory::createFormLayout(this);

  // Name.
  m_nameLineEdit = WidgetsFactory::createLineEdit(this);
  auto validator = new QRegularExpressionValidator(
      QRegularExpression(PathUtils::c_fileNameRegularExpression), m_nameLineEdit);
  m_nameLineEdit->setValidator(validator);
  connect(m_nameLineEdit, &QLineEdit::textEdited, this, &SnippetInfoWidget2::inputEdited);
  mainLayout->addRow(tr("Name:"), m_nameLineEdit);

  setFocusProxy(m_nameLineEdit);

  // Description.
  m_descriptionLineEdit = WidgetsFactory::createLineEdit(this);
  connect(m_descriptionLineEdit, &QLineEdit::textEdited, this, &SnippetInfoWidget2::inputEdited);
  mainLayout->addRow(tr("Description:"), m_descriptionLineEdit);

  // Type.
  setupTypeComboBox();
  mainLayout->addRow(tr("Type:"), m_typeComboBox);

  // Shortcut.
  setupShortcutComboBox();
  mainLayout->addRow(tr("Shortcut:"), m_shortcutComboBox);

  // Cursor mark.
  m_cursorMarkLineEdit = WidgetsFactory::createLineEdit(this);
  m_cursorMarkLineEdit->setText(QStringLiteral("@@"));
  m_cursorMarkLineEdit->setToolTip(
      tr("A mark in the snippet content indicating the cursor position after the application"));
  connect(m_cursorMarkLineEdit, &QLineEdit::textEdited, this, &SnippetInfoWidget2::inputEdited);
  mainLayout->addRow(tr("Cursor mark:"), m_cursorMarkLineEdit);

  // Selection mark.
  m_selectionMarkLineEdit = WidgetsFactory::createLineEdit(this);
  m_selectionMarkLineEdit->setText(QStringLiteral("$$"));
  m_selectionMarkLineEdit->setToolTip(tr("A mark in the snippet content that will be replaced with "
                                         "the selected text before the application"));
  connect(m_selectionMarkLineEdit, &QLineEdit::textEdited, this, &SnippetInfoWidget2::inputEdited);
  mainLayout->addRow(tr("Selection mark:"), m_selectionMarkLineEdit);

  // Indent as first line.
  m_indentAsFirstLineCheckBox = WidgetsFactory::createCheckBox(tr("Indent as first line"), this);
  m_indentAsFirstLineCheckBox->setChecked(true);
  connect(m_indentAsFirstLineCheckBox, &QCheckBox::stateChanged, this,
          &SnippetInfoWidget2::inputEdited);
  mainLayout->addRow(m_indentAsFirstLineCheckBox);

  // Content.
  m_contentTextEdit = WidgetsFactory::createPlainTextEdit(this);
  m_contentTextEdit->setPlaceholderText(
      tr("Nested snippet is supported, like `%time%` to embed the snippet `time`"));
  connect(m_contentTextEdit, &QPlainTextEdit::textChanged, this, &SnippetInfoWidget2::inputEdited);
  mainLayout->addRow(tr("Content:"), m_contentTextEdit);
}

void SnippetInfoWidget2::setupTypeComboBox() {
  m_typeComboBox = WidgetsFactory::createComboBox(this);
  m_typeComboBox->addItem(tr("Text"), QStringLiteral("text"));
  if (m_editMode) {
    m_typeComboBox->addItem(tr("Dynamic"), QStringLiteral("dynamic"));
  }
  connect(m_typeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this]() { emit inputEdited(); });
}

void SnippetInfoWidget2::setupShortcutComboBox() {
  m_shortcutComboBox = WidgetsFactory::createComboBox(this);
  // Add default "None" item.
  m_shortcutComboBox->addItem(tr("None"), -1);
  connect(m_shortcutComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &SnippetInfoWidget2::inputEdited);
}

QString SnippetInfoWidget2::getName() const { return m_nameLineEdit->text(); }

QString SnippetInfoWidget2::getDescription() const { return m_descriptionLineEdit->text(); }

QString SnippetInfoWidget2::getType() const { return m_typeComboBox->currentData().toString(); }

int SnippetInfoWidget2::getShortcut() const { return m_shortcutComboBox->currentData().toInt(); }

QJsonObject SnippetInfoWidget2::toContentJson() const {
  QJsonObject obj;
  obj[QStringLiteral("type")] = getType();
  obj[QStringLiteral("description")] = getDescription();
  obj[QStringLiteral("content")] = m_contentTextEdit->toPlainText();
  obj[QStringLiteral("cursorMark")] = m_cursorMarkLineEdit->text();
  obj[QStringLiteral("selectionMark")] = m_selectionMarkLineEdit->text();
  obj[QStringLiteral("indentAsFirstLine")] = m_indentAsFirstLineCheckBox->isChecked();
  return obj;
}

void SnippetInfoWidget2::setReadOnly(bool p_readOnly) {
  m_nameLineEdit->setEnabled(!p_readOnly);
  m_descriptionLineEdit->setEnabled(!p_readOnly);
  m_typeComboBox->setEnabled(!p_readOnly);
  m_shortcutComboBox->setEnabled(!p_readOnly);
  m_cursorMarkLineEdit->setEnabled(!p_readOnly);
  m_selectionMarkLineEdit->setEnabled(!p_readOnly);
  m_indentAsFirstLineCheckBox->setEnabled(!p_readOnly);
  m_contentTextEdit->setEnabled(!p_readOnly);
}

void SnippetInfoWidget2::populateShortcuts(const QList<int> &p_available, int p_current) {
  m_shortcutComboBox->clear();
  m_shortcutComboBox->addItem(tr("None"), -1);

  for (auto sh : p_available) {
    m_shortcutComboBox->addItem(Utils::intToString(sh, 2), sh);
  }

  if (p_current >= 0) {
    // Add the current shortcut if not already in the available list.
    if (m_shortcutComboBox->findData(p_current) == -1) {
      m_shortcutComboBox->addItem(Utils::intToString(p_current, 2), p_current);
    }
    int idx = m_shortcutComboBox->findData(p_current);
    if (idx >= 0) {
      m_shortcutComboBox->setCurrentIndex(idx);
    }
  }
}
