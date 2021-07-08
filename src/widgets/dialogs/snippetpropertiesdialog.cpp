#include "snippetpropertiesdialog.h"

#include <snippet/snippet.h>
#include <snippet/snippetmgr.h>
#include <core/exception.h>

#include "snippetinfowidget.h"

using namespace vnotex;

SnippetPropertiesDialog::SnippetPropertiesDialog(Snippet *p_snippet, QWidget *p_parent)
    : ScrollDialog(p_parent),
      m_snippet(p_snippet)
{
    Q_ASSERT(m_snippet);
    setupUI();

    m_infoWidget->setFocus();
}

void SnippetPropertiesDialog::setupUI()
{
    setupSnippetInfoWidget(this);
    setCentralWidget(m_infoWidget);

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    if (m_snippet->isReadOnly()) {
        setButtonEnabled(QDialogButtonBox::Ok, false);
    }

    setWindowTitle(tr("%1 Properties").arg(m_snippet->getName()));
}

void SnippetPropertiesDialog::setupSnippetInfoWidget(QWidget *p_parent)
{
    m_infoWidget = new SnippetInfoWidget(m_snippet, p_parent);
}

bool SnippetPropertiesDialog::validateInputs()
{
    bool valid = true;
    QString msg;

    valid = valid && validateNameInput(msg);
    setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                  : ScrollDialog::InformationLevel::Error);
    return valid;
}

bool SnippetPropertiesDialog::validateNameInput(QString &p_msg)
{
    p_msg.clear();

    auto name = m_infoWidget->getName();
    if (name.isEmpty()) {
        p_msg = tr("Please specify a name for the snippet.");
        return false;
    }

    if (name.toLower() == m_snippet->getName().toLower()) {
        return true;
    }

    if (SnippetMgr::getInst().find(name)) {
        p_msg = tr("Name conflicts with existing snippet.");
        return false;
    }

    return true;
}

void SnippetPropertiesDialog::acceptedButtonClicked()
{
    if (validateInputs() && saveSnippetProperties()) {
        accept();
    }
}

bool SnippetPropertiesDialog::saveSnippetProperties()
{
    auto snip = QSharedPointer<Snippet>::create(m_infoWidget->getName(),
                                                m_infoWidget->getDescription(),
                                                m_infoWidget->getContent(),
                                                m_infoWidget->getShortcut(),
                                                m_infoWidget->shouldIndentAsFirstLine(),
                                                m_infoWidget->getCursorMark(),
                                                m_infoWidget->getSelectionMark());
    Q_ASSERT(snip->isValid());
    try {
        SnippetMgr::getInst().updateSnippet(m_snippet->getName(), snip);
    } catch (Exception &p_e) {
        QString msg = tr("Failed to update snippet (%1) (%2).")
                        .arg(snip->getName(), p_e.what());
        qWarning() << msg;
        setInformationText(msg, ScrollDialog::InformationLevel::Error);
        return false;
    }
    return true;
}
