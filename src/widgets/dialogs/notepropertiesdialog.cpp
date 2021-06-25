#include "notepropertiesdialog.h"

#include "notebook/notebook.h"
#include "notebook/node.h"
#include "../widgetsfactory.h"
#include <utils/pathutils.h>
#include "exception.h"
#include "nodeinfowidget.h"
#include "../lineedit.h"
#include <core/events.h>
#include <core/vnotex.h>

using namespace vnotex;

NotePropertiesDialog::NotePropertiesDialog(Node *p_node, QWidget *p_parent)
    : ScrollDialog(p_parent),
      m_node(p_node)
{
    Q_ASSERT(m_node);
    setupUI();

    LineEdit::selectBaseName(m_infoWidget->getNameLineEdit());

    m_infoWidget->getNameLineEdit()->setFocus();
}

void NotePropertiesDialog::setupUI()
{
    setupNodeInfoWidget(this);
    setCentralWidget(m_infoWidget);

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    setWindowTitle(tr("%1 Properties").arg(m_node->getName()));
}

void NotePropertiesDialog::setupNodeInfoWidget(QWidget *p_parent)
{
    m_infoWidget = new NodeInfoWidget(m_node, p_parent);
}

bool NotePropertiesDialog::validateInputs()
{
    bool valid = true;
    QString msg;

    valid = valid && validateNameInput(msg);
    setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                  : ScrollDialog::InformationLevel::Error);
    return valid;
}

bool NotePropertiesDialog::validateNameInput(QString &p_msg)
{
    p_msg.clear();

    auto name = m_infoWidget->getName();
    if (name.isEmpty() || !PathUtils::isLegalFileName(name)) {
        p_msg = tr("Please specify a valid name for the note.");
        return false;
    }

    Q_ASSERT(m_infoWidget->getParentNode() == m_node->getParent());
    if (!m_node->canRename(name)) {
        p_msg = tr("Name conflicts with existing note.");
        return false;
    }

    return true;
}

void NotePropertiesDialog::acceptedButtonClicked()
{
    if (validateInputs() && saveNoteProperties()) {
        accept();
    }
}

bool NotePropertiesDialog::saveNoteProperties()
{
    try {
        if (m_infoWidget->getName() != m_node->getName()) {
            // Close the node first.
            auto event = QSharedPointer<Event>::create();
            emit VNoteX::getInst().nodeAboutToRename(m_node, event);
            if (!event->m_response.toBool()) {
                return false;
            }

            m_node->updateName(m_infoWidget->getName());
        }
    } catch (Exception &p_e) {
        QString msg = tr("Failed to save note (%1) in (%2) (%3).").arg(m_node->getName(),
                                                                       m_node->getNotebook()->getName(),
                                                                       p_e.what());
        qCritical() << msg;
        setInformationText(msg, ScrollDialog::InformationLevel::Error);
        return false;
    }

    return true;
}
