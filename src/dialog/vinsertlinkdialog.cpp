#include "vinsertlinkdialog.h"

#include <QtWidgets>

#include "vmetawordlineedit.h"

VInsertLinkDialog::VInsertLinkDialog(const QString &p_title,
                                     const QString &p_text,
                                     const QString &p_info,
                                     const QString &p_linkText,
                                     const QString &p_linkUrl,
                                     QWidget *p_parent)
    : QDialog(p_parent)
{
    setupUI(p_title, p_text, p_info, p_linkText, p_linkUrl);

    fetchLinkFromClipboard();

    handleInputChanged();
}

void VInsertLinkDialog::setupUI(const QString &p_title,
                                const QString &p_text,
                                const QString &p_info,
                                const QString &p_linkText,
                                const QString &p_linkUrl)
{
    QLabel *textLabel = NULL;
    if (!p_text.isEmpty()) {
        textLabel = new QLabel(p_text);
        textLabel->setWordWrap(true);
    }

    QLabel *infoLabel = NULL;
    if (!p_info.isEmpty()) {
        infoLabel = new QLabel(p_info);
        infoLabel->setWordWrap(true);
    }

    m_linkTextEdit = new VMetaWordLineEdit(p_linkText);
    m_linkTextEdit->selectAll();

    m_linkUrlEdit = new VLineEdit(p_linkUrl);
    m_linkUrlEdit->setToolTip(tr("Absolute or relative path of the link"));

    QFormLayout *inputLayout = new QFormLayout();
    inputLayout->addRow(tr("&Text:"), m_linkTextEdit);
    inputLayout->addRow(tr("&URL:"), m_linkUrlEdit);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setProperty("SpecialBtn", true);
    m_linkTextEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    if (textLabel) {
        mainLayout->addWidget(textLabel);
    }

    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }

    mainLayout->addLayout(inputLayout);
    mainLayout->addWidget(m_btnBox);

    setLayout(mainLayout);
    setWindowTitle(p_title);

    connect(m_linkTextEdit, &VMetaWordLineEdit::textChanged,
            this, &VInsertLinkDialog::handleInputChanged);
    connect(m_linkUrlEdit, &VLineEdit::textChanged,
            this, &VInsertLinkDialog::handleInputChanged);
}

void VInsertLinkDialog::handleInputChanged()
{
    bool textOk = true;
    if (m_linkTextEdit->getEvaluatedText().isEmpty()) {
        textOk = false;
    }

    bool urlOk = true;
    if (m_linkUrlEdit->text().isEmpty()) {
        urlOk = false;
    }

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(textOk && urlOk);
}

void VInsertLinkDialog::fetchLinkFromClipboard()
{
    if (!m_linkUrlEdit->text().isEmpty()
        || !m_linkTextEdit->text().isEmpty()) {
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    if (!mimeData->hasText()) {
        return;
    }

    QString text = mimeData->text();
    text = text.trimmed();
    if (text.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(text);
    if (url.isValid()) {
        if (m_linkUrlEdit->text().isEmpty()) {
            m_linkUrlEdit->setText(text);
        }
    } else if (m_linkTextEdit->text().isEmpty()) {
        m_linkTextEdit->setText(text);
    }
}

QString VInsertLinkDialog::getLinkText() const
{
    return m_linkTextEdit->getEvaluatedText();
}

QString VInsertLinkDialog::getLinkUrl() const
{
    return m_linkUrlEdit->text();
}

void VInsertLinkDialog::showEvent(QShowEvent *p_event)
{
    QDialog::showEvent(p_event);

    if (!m_linkTextEdit->text().isEmpty() && m_linkUrlEdit->text().isEmpty()) {
        m_linkUrlEdit->setFocus();
    }
}
