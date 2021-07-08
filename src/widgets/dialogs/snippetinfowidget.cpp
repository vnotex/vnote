#include "snippetinfowidget.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QFormLayout>
#include <QCheckBox>

#include <widgets/widgetsfactory.h>
#include <snippet/snippet.h>
#include <snippet/snippetmgr.h>
#include <utils/utils.h>
#include <utils/pathutils.h>

using namespace vnotex;

SnippetInfoWidget::SnippetInfoWidget(QWidget *p_parent)
    : QWidget(p_parent),
      m_mode(Mode::Create)
{
    setupUI();
}

SnippetInfoWidget::SnippetInfoWidget(const Snippet *p_snippet, QWidget *p_parent)
    : QWidget(p_parent),
      m_mode(Mode::Edit)
{
    setupUI();

    setSnippet(p_snippet);
}

void SnippetInfoWidget::setupUI()
{
    auto mainLayout = new QFormLayout(this);

    m_nameLineEdit = WidgetsFactory::createLineEdit(this);
    auto validator = new QRegularExpressionValidator(QRegularExpression(PathUtils::c_fileNameRegularExpression),
                                                     m_nameLineEdit);
    m_nameLineEdit->setValidator(validator);
    connect(m_nameLineEdit, &QLineEdit::textEdited,
            this, &SnippetInfoWidget::inputEdited);
    mainLayout->addRow(tr("Name:"), m_nameLineEdit);

    setFocusProxy(m_nameLineEdit);

    m_descriptionLineEdit = WidgetsFactory::createLineEdit(this);
    connect(m_descriptionLineEdit, &QLineEdit::textEdited,
            this, &SnippetInfoWidget::inputEdited);
    mainLayout->addRow(tr("Description:"), m_descriptionLineEdit);

    setupTypeComboBox(this);
    mainLayout->addRow(tr("Type:"), m_typeComboBox);

    setupShortcutComboBox(this);
    mainLayout->addRow(tr("Shortcut:"), m_shortcutComboBox);

    m_cursorMarkLineEdit = WidgetsFactory::createLineEdit(this);
    m_cursorMarkLineEdit->setText(Snippet::c_defaultCursorMark);
    m_cursorMarkLineEdit->setToolTip(tr("A mark in the snippet content indicating the cursor position after the application"));
    connect(m_cursorMarkLineEdit, &QLineEdit::textEdited,
            this, &SnippetInfoWidget::inputEdited);
    mainLayout->addRow(tr("Cursor mark:"), m_cursorMarkLineEdit);

    m_selectionMarkLineEdit = WidgetsFactory::createLineEdit(this);
    m_selectionMarkLineEdit->setText(Snippet::c_defaultSelectionMark);
    m_selectionMarkLineEdit->setToolTip(tr("A mark in the snippet content that will be replaced with the selected text before the application"));
    connect(m_selectionMarkLineEdit, &QLineEdit::textEdited,
            this, &SnippetInfoWidget::inputEdited);
    mainLayout->addRow(tr("Selection mark:"), m_selectionMarkLineEdit);

    m_indentAsFirstLineCheckBox = WidgetsFactory::createCheckBox(tr("Indent as first line"), this);
    m_indentAsFirstLineCheckBox->setChecked(true);
    connect(m_indentAsFirstLineCheckBox, &QCheckBox::stateChanged,
            this, &SnippetInfoWidget::inputEdited);
    mainLayout->addRow(m_indentAsFirstLineCheckBox);

    m_contentTextEdit = WidgetsFactory::createPlainTextEdit(this);
    connect(m_contentTextEdit, &QPlainTextEdit::textChanged,
            this, &SnippetInfoWidget::inputEdited);
    mainLayout->addRow(tr("Content:"), m_contentTextEdit);
}

void SnippetInfoWidget::setupTypeComboBox(QWidget *p_parent)
{
    m_typeComboBox = WidgetsFactory::createComboBox(p_parent);
    m_typeComboBox->addItem(tr("Text"), static_cast<int>(Snippet::Type::Text));
    if (m_mode == Mode::Edit) {
        m_typeComboBox->addItem(tr("Dynamic"), static_cast<int>(Snippet::Type::Dynamic));
    }
    connect(m_typeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
                emit inputEdited();
            });
}

void SnippetInfoWidget::setupShortcutComboBox(QWidget *p_parent)
{
    m_shortcutComboBox = WidgetsFactory::createComboBox(p_parent);
    if (m_mode == Mode::Create) {
        initShortcutComboBox();
    }
    connect(m_shortcutComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SnippetInfoWidget::inputEdited);
}

QString SnippetInfoWidget::getName() const
{
    return m_nameLineEdit->text();
}

Snippet::Type SnippetInfoWidget::getType() const
{
    return static_cast<Snippet::Type>(m_typeComboBox->currentData().toInt());
}

int SnippetInfoWidget::getShortcut() const
{
    return m_shortcutComboBox->currentData().toInt();
}

QString SnippetInfoWidget::getCursorMark() const
{
    return m_cursorMarkLineEdit->text();
}

QString SnippetInfoWidget::getSelectionMark() const
{
    return m_selectionMarkLineEdit->text();
}

bool SnippetInfoWidget::shouldIndentAsFirstLine() const
{
    return m_indentAsFirstLineCheckBox->isChecked();
}

QString SnippetInfoWidget::getContent() const
{
    return m_contentTextEdit->toPlainText();
}

QString SnippetInfoWidget::getDescription() const
{
    return m_descriptionLineEdit->text();
}

void SnippetInfoWidget::setSnippet(const Snippet *p_snippet)
{
    if (m_snippet == p_snippet) {
        return;
    }

    Q_ASSERT(m_mode == Mode::Edit);
    m_snippet = p_snippet;
    initShortcutComboBox();
    if (m_snippet) {
        const bool readOnly = m_snippet->isReadOnly();
        m_nameLineEdit->setText(m_snippet->getName());
        m_nameLineEdit->setEnabled(!readOnly);
        m_descriptionLineEdit->setText(m_snippet->getDescription());
        m_descriptionLineEdit->setEnabled(!readOnly);
        m_typeComboBox->setCurrentIndex(m_typeComboBox->findData(static_cast<int>(m_snippet->getType())));
        m_typeComboBox->setEnabled(!readOnly);
        m_shortcutComboBox->setCurrentIndex(m_shortcutComboBox->findData(m_snippet->getShortcut()));
        m_shortcutComboBox->setEnabled(!readOnly);
        m_cursorMarkLineEdit->setText(m_snippet->getCursorMark());
        m_cursorMarkLineEdit->setEnabled(!readOnly);
        m_selectionMarkLineEdit->setText(m_snippet->getSelectionMark());
        m_selectionMarkLineEdit->setEnabled(!readOnly);
        m_indentAsFirstLineCheckBox->setChecked(m_snippet->isIndentAsFirstLineEnabled());
        m_indentAsFirstLineCheckBox->setEnabled(!readOnly);
        m_contentTextEdit->setPlainText(m_snippet->getContent());
        m_contentTextEdit->setEnabled(!readOnly);
    } else {
        m_nameLineEdit->clear();
        m_descriptionLineEdit->clear();
        m_typeComboBox->setCurrentIndex(m_typeComboBox->findData(static_cast<int>(Snippet::Type::Text)));
        m_shortcutComboBox->setCurrentIndex(m_shortcutComboBox->findData(Snippet::InvalidShortcut));
        m_cursorMarkLineEdit->setText(Snippet::c_defaultCursorMark);
        m_selectionMarkLineEdit->setText(Snippet::c_defaultSelectionMark);
        m_indentAsFirstLineCheckBox->setChecked(true);
        m_contentTextEdit->clear();
    }
}

void SnippetInfoWidget::initShortcutComboBox()
{
    m_shortcutComboBox->clear();
    m_shortcutComboBox->addItem(tr("None"), Snippet::InvalidShortcut);
    const auto shortcuts = SnippetMgr::getInst().getAvailableShortcuts(m_snippet ? m_snippet->getShortcut() : Snippet::InvalidShortcut);
    for (auto sh : shortcuts) {
        m_shortcutComboBox->addItem(Utils::intToString(sh, 2), sh);
    }
}
