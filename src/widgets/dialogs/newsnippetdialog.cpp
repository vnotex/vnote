#include "newsnippetdialog.h"

#include "snippetinfowidget.h"

#include <snippet/snippetmgr.h>
#include <core/exception.h>

using namespace vnotex;

NewSnippetDialog::NewSnippetDialog(QWidget *p_parent)
    : ScrollDialog(p_parent)
{
    setupUI();

    m_infoWidget->setFocus();
}

void NewSnippetDialog::setupUI()
{
    setupSnippetInfoWidget(this);
    setCentralWidget(m_infoWidget);

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    setWindowTitle(tr("New Snippet"));
}

void NewSnippetDialog::setupSnippetInfoWidget(QWidget *p_parent)
{
    m_infoWidget = new SnippetInfoWidget(p_parent);
}

bool NewSnippetDialog::validateInputs()
{
    bool valid = true;
    QString msg;

    valid = valid && validateNameInput(msg);
    setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                  : ScrollDialog::InformationLevel::Error);
    return valid;
}

void NewSnippetDialog::acceptedButtonClicked()
{
    if (validateInputs() && newSnippet()) {
        accept();
    }
}

bool NewSnippetDialog::newSnippet()
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
        SnippetMgr::getInst().addSnippet(snip);
    } catch (Exception &p_e) {
        QString msg = tr("Failed to add snippet (%1) (%2).")
                        .arg(snip->getName(), p_e.what());
        qWarning() << msg;
        setInformationText(msg, ScrollDialog::InformationLevel::Error);
        return false;
    }
    return true;
}

bool NewSnippetDialog::validateNameInput(QString &p_msg)
{
    p_msg.clear();

    const auto name = m_infoWidget->getName();
    if (name.isEmpty()) {
        p_msg = tr("Please specify a name for the snippet.");
        return false;
    }

    if (SnippetMgr::getInst().find(name, Qt::CaseInsensitive)) {
        p_msg = tr("Name conflicts with existing snippet.");
        return false;
    }

    return true;
}
