#include "linkinsertdialog.h"

#include <QFormLayout>
#include <QUrl>
#include <QRegularExpression>

#include <widgets/widgetsfactory.h>
#include <widgets/lineedit.h>
#include <utils/widgetutils.h>

using namespace vnotex;

LinkInsertDialog::LinkInsertDialog(const QString &p_title,
                                   const QString &p_linkText,
                                   const QString &p_linkUrl,
                                   bool p_linkTextOptional,
                                   QWidget *p_parent)
    : ScrollDialog(p_parent),
      m_linkTextOptional(p_linkTextOptional)
{
    setupUI(p_title, p_linkText, p_linkUrl);

    checkInput();
}

void LinkInsertDialog::setupUI(const QString &p_title,
                               const QString &p_linkText,
                               const QString &p_linkUrl)
{
    auto mainWidget = new QWidget(this);
    setCentralWidget(mainWidget);

    auto mainLayout = WidgetUtils::createFormLayout(mainWidget);

    m_linkTextEdit = WidgetsFactory::createLineEdit(p_linkText, mainWidget);
    mainLayout->addRow(tr("&Text:"), m_linkTextEdit);
    connect(m_linkTextEdit, &QLineEdit::textChanged,
            this, [this]() {
                checkInput(false);
            });

    m_linkUrlEdit = WidgetsFactory::createLineEdit(p_linkUrl, mainWidget);
    mainLayout->addRow(tr("&Url:"), m_linkUrlEdit);
    connect(m_linkUrlEdit, &QLineEdit::textChanged,
            this, [this]() {
                checkInput(true);
            });

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    setWindowTitle(p_title);
}

void LinkInsertDialog::checkInput(bool p_autoCompleteText)
{
    bool ok = true;

    const auto linkUrl = m_linkUrlEdit->text();
    if (linkUrl.isEmpty()) {
        ok = false;
    } else {
        const auto linkText = m_linkTextEdit->text();
        if (linkText.isEmpty()) {
            // Try to guess the text from url.
            if (p_autoCompleteText) {
                int idx = linkUrl.lastIndexOf(QRegularExpression(QStringLiteral("[/\\\\]")));
                if (idx != -1 && idx != linkUrl.size() - 1) {
                    m_linkTextEdit->setText(linkUrl.mid(idx + 1));
                } else {
                    ok = m_linkTextOptional;
                }
            } else {
                ok = m_linkTextOptional;
            }
        }
    }

    setButtonEnabled(QDialogButtonBox::Ok, ok);
}

QString LinkInsertDialog::getLinkText() const
{
    return m_linkTextEdit->text();
}

QString LinkInsertDialog::getLinkUrl() const
{
    // For local file, translate to URL without spaces.
    auto text = m_linkUrlEdit->text();
    if (text.isEmpty()) {
        return text;
    }

    auto url = QUrl::fromUserInput(text);
    if (url.isLocalFile()) {
        return url.toString(QUrl::EncodeSpaces);
    }
    return text;
}

void LinkInsertDialog::showEvent(QShowEvent *p_event)
{
    ScrollDialog::showEvent(p_event);

    if (!m_linkUrlEdit->text().isEmpty() || m_linkTextEdit->text().isEmpty()) {
        m_linkTextEdit->setFocus();
        m_linkTextEdit->selectAll();
    } else {
        m_linkUrlEdit->setFocus();
        m_linkUrlEdit->selectAll();
    }
}
