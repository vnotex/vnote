#include "veditsnippetdialog.h"
#include <QtWidgets>

#include "utils/vutils.h"
#include "vlineedit.h"
#include "vconfigmanager.h"
#include "utils/vmetawordmanager.h"

extern VMetaWordManager *g_mwMgr;

extern VConfigManager *g_config;

VEditSnippetDialog::VEditSnippetDialog(const QString &p_title,
                                       const QString &p_info,
                                       const QVector<VSnippet> &p_snippets,
                                       const VSnippet &p_snippet,
                                       QWidget *p_parent)
    : QDialog(p_parent),
      m_snippets(p_snippets),
      m_snippet(p_snippet)
{
    setupUI(p_title, p_info);

    handleInputChanged();
}

void VEditSnippetDialog::setupUI(const QString &p_title, const QString &p_info)
{
    QLabel *infoLabel = NULL;
    if (!p_info.isEmpty()) {
        infoLabel = new QLabel(p_info);
        infoLabel->setWordWrap(true);
    }

    // Name.
    m_nameEdit = new VLineEdit(m_snippet.getName());
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_nameEdit);
    m_nameEdit->setValidator(validator);

    // Type.
    m_typeCB = VUtils::getComboBox();
    for (int i = 0; i < VSnippet::Type::Invalid; ++i) {
        m_typeCB->addItem(VSnippet::typeStr(static_cast<VSnippet::Type>(i)), i);
    }

    int typeIdx = m_typeCB->findData((int)m_snippet.getType());
    Q_ASSERT(typeIdx > -1);
    m_typeCB->setCurrentIndex(typeIdx);

    // Shortcut.
    m_shortcutCB = VUtils::getComboBox();
    m_shortcutCB->addItem(tr("None"), QChar());
    auto shortcuts = getAvailableShortcuts();
    for (auto it : shortcuts) {
        m_shortcutCB->addItem(it, it);
    }

    QChar sh = m_snippet.getShortcut();
    if (sh.isNull()) {
        m_shortcutCB->setCurrentIndex(0);
    } else {
        int shortcutIdx = m_shortcutCB->findData(sh);
        m_shortcutCB->setCurrentIndex(shortcutIdx < 0 ? 0 : shortcutIdx);
    }

    // Cursor mark.
    m_cursorMarkEdit = new QLineEdit(m_snippet.getCursorMark());
    m_cursorMarkEdit->setToolTip(tr("String in the content to mark the cursor position"));

    // Selection mark.
    m_selectionMarkEdit = new QLineEdit(m_snippet.getSelectionMark());
    m_selectionMarkEdit->setToolTip(tr("String in the content to be replaced with selected text"));

    // Auto Indent.
    m_autoIndentCB = new QCheckBox(tr("Auto indent"), this);
    m_autoIndentCB->setToolTip(tr("Auto indent the content according to the first line"));
    m_autoIndentCB->setChecked(m_snippet.getAutoIndent());

    // Content.
    m_contentEdit = new QTextEdit();
    setContentEditByType();

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(tr("Snippet &name:"), m_nameEdit);
    topLayout->addRow(tr("Snippet &type:"), m_typeCB);
    topLayout->addRow(tr("Shortc&ut:"), m_shortcutCB);
    topLayout->addRow(tr("Cursor &mark:"), m_cursorMarkEdit);
    topLayout->addRow(tr("&Selection mark:"), m_selectionMarkEdit);
    topLayout->addWidget(m_autoIndentCB);
    topLayout->addRow(tr("&Content:"), m_contentEdit);

    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    m_nameEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_warnLabel);
    mainLayout->addWidget(m_btnBox);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setLayout(mainLayout);

    setWindowTitle(p_title);

    connect(m_nameEdit, &QLineEdit::textChanged,
            this, &VEditSnippetDialog::handleInputChanged);

    connect(m_typeCB, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &VEditSnippetDialog::handleInputChanged);

    connect(m_cursorMarkEdit, &QLineEdit::textChanged,
            this, &VEditSnippetDialog::handleInputChanged);

    connect(m_selectionMarkEdit, &QLineEdit::textChanged,
            this, &VEditSnippetDialog::handleInputChanged);

    connect(m_contentEdit, &QTextEdit::textChanged,
            this, &VEditSnippetDialog::handleInputChanged);
}

void VEditSnippetDialog::handleInputChanged()
{
    bool showWarnLabel = false;
    QString name = m_nameEdit->getEvaluatedText();
    bool nameOk = !name.isEmpty();
    if (nameOk && name != m_snippet.getName()) {
        // Check if the name conflicts with existing snippet name.
        // Case-insensitive.
        QString lowerName = name.toLower();
        bool conflicted = false;
        for (auto const & item : m_snippets) {
            if (item.getName() == name) {
                conflicted = true;
                break;
            }
        }

        QString warnText;
        if (conflicted) {
            nameOk = false;
            warnText = tr("<span style=\"%1\">WARNING</span>: "
                          "Name (case-insensitive) <span style=\"%2\">%3</span> already exists. "
                          "Please choose another name.")
                         .arg(g_config->c_warningTextStyle)
                         .arg(g_config->c_dataTextStyle)
                         .arg(name);
        } else if (!VUtils::checkFileNameLegal(name)) {
            // Check if evaluated name contains illegal characters.
            nameOk = false;
            warnText = tr("<span style=\"%1\">WARNING</span>: "
                          "Name <span style=\"%2\">%3</span> contains illegal characters "
                          "(after magic word evaluation).")
                         .arg(g_config->c_warningTextStyle)
                         .arg(g_config->c_dataTextStyle)
                         .arg(name);
        }

        if (!nameOk) {
            showWarnLabel = true;
            m_warnLabel->setText(warnText);
        }
    }

    QString cursorMark = m_cursorMarkEdit->text();
    bool cursorMarkOk = true;
    if (nameOk && !cursorMark.isEmpty()) {
        // Check if the mark appears more than once in the content.
        QString selectionMark = m_selectionMarkEdit->text();
        QString content = getContentEditByType();
        content = g_mwMgr->evaluate(content);
        QString warnText;
        if (content.count(cursorMark) > 1) {
            cursorMarkOk = false;
            warnText = tr("<span style=\"%1\">WARNING</span>: "
                          "Cursor mark <span style=\"%2\">%3</span> occurs more than once "
                          "in the content (after magic word evaluation). "
                          "Please choose another mark.")
                         .arg(g_config->c_warningTextStyle)
                         .arg(g_config->c_dataTextStyle)
                         .arg(cursorMark);
        } else if ((cursorMark == selectionMark
                    || cursorMark.contains(selectionMark)
                    || selectionMark.contains(cursorMark))
                   && !selectionMark.isEmpty()) {
            cursorMarkOk = false;
            warnText = tr("<span style=\"%1\">WARNING</span>: "
                          "Cursor mark <span style=\"%2\">%3</span> conflicts with selection mark. "
                          "Please choose another mark.")
                         .arg(g_config->c_warningTextStyle)
                         .arg(g_config->c_dataTextStyle)
                         .arg(cursorMark);
        }

        if (!cursorMarkOk) {
            showWarnLabel = true;
            m_warnLabel->setText(warnText);
        }
    }

    m_warnLabel->setVisible(showWarnLabel);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(nameOk && cursorMarkOk);
}

void VEditSnippetDialog::setContentEditByType()
{
    switch (m_snippet.getType()) {
    case VSnippet::Type::PlainText:
        m_contentEdit->setPlainText(m_snippet.getContent());
        break;

    case VSnippet::Type::Html:
        m_contentEdit->setHtml(m_snippet.getContent());
        break;

    default:
        m_contentEdit->setPlainText(m_snippet.getContent());
        break;
    }
}

QString VEditSnippetDialog::getContentEditByType() const
{
    if (m_typeCB->currentIndex() == -1) {
        return QString();
    }

    switch (static_cast<VSnippet::Type>(m_typeCB->currentData().toInt())) {
    case VSnippet::Type::PlainText:
        return m_contentEdit->toPlainText();

    case VSnippet::Type::Html:
        return m_contentEdit->toHtml();

    default:
        return m_contentEdit->toPlainText();
    }
}

QString VEditSnippetDialog::getNameInput() const
{
    return m_nameEdit->getEvaluatedText();
}

VSnippet::Type VEditSnippetDialog::getTypeInput() const
{
    return static_cast<VSnippet::Type>(m_typeCB->currentData().toInt());
}

QString VEditSnippetDialog::getCursorMarkInput() const
{
    return m_cursorMarkEdit->text();
}

QString VEditSnippetDialog::getSelectionMarkInput() const
{
    return m_selectionMarkEdit->text();
}

QString VEditSnippetDialog::getContentInput() const
{
    return getContentEditByType();
}

QChar VEditSnippetDialog::getShortcutInput() const
{
    return m_shortcutCB->currentData().toChar();
}

bool VEditSnippetDialog::getAutoIndentInput() const
{
    return m_autoIndentCB->isChecked();
}

QVector<QChar> VEditSnippetDialog::getAvailableShortcuts() const
{
    QVector<QChar> ret = VSnippet::getAllShortcuts();

    QChar curCh = m_snippet.getShortcut();
    // Remove those have already been assigned to snippets.
    for (auto const & snip : m_snippets) {
        QChar ch = snip.getShortcut();
        if (ch.isNull()) {
            continue;
        }

        if (ch != curCh) {
            ret.removeOne(ch);
        }
    }

    return ret;
}
