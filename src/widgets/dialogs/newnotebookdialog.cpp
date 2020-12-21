#include "newnotebookdialog.h"

#include <QLineEdit>

#include "../widgetsfactory.h"
#include "vnotex.h"
#include "notebookmgr.h"
#include "notebook/inotebookfactory.h"
#include "notebook/notebookparameters.h"
#include "configmgr.h"
#include <utils/pathutils.h>
#include <utils/utils.h>
#include "exception.h"
#include "notebookinfowidget.h"

using namespace vnotex;

NewNotebookDialog::NewNotebookDialog(QWidget *p_parent)
    : ScrollDialog(p_parent)
{
    setupUI();

    m_infoWidget->getNameLineEdit()->setFocus();
}

void NewNotebookDialog::setupUI()
{
    setupNotebookInfoWidget(this);
    setCentralWidget(m_infoWidget);

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    setButtonEnabled(QDialogButtonBox::Ok, false);

    setWindowTitle(tr("New Notebook"));
}

void NewNotebookDialog::setupNotebookInfoWidget(QWidget *p_parent)
{
    m_infoWidget = new NotebookInfoWidget(NotebookInfoWidget::Create, p_parent);
    connect(m_infoWidget, &NotebookInfoWidget::rootFolderEdited,
            this, &NewNotebookDialog::handleRootFolderPathChanged);
    connect(m_infoWidget, &NotebookInfoWidget::basicInfoEdited,
            this, &NewNotebookDialog::validateInputs);

    {
        auto whatsThis = tr("<br/>Both absolute and relative paths are supported. ~ and environment variable are not supported now.");
        auto rootFolderEdit = m_infoWidget->getRootFolderPathLineEdit();
        rootFolderEdit->setWhatsThis(rootFolderEdit->whatsThis() + whatsThis);
    }
}

void NewNotebookDialog::validateInputs()
{
    bool valid = true;
    QString msg;

    valid = valid && validateNameInput(msg);
    valid = valid && validateRootFolderInput(msg);

    setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                  : ScrollDialog::InformationLevel::Error);
    setButtonEnabled(QDialogButtonBox::Ok, valid);
}

bool NewNotebookDialog::validateNameInput(QString &p_msg)
{
    if (m_infoWidget->getName().isEmpty()) {
        Utils::appendMsg(p_msg, tr("Please specify a name for the notebook."));
        return false;
    }

    return true;
}

bool NewNotebookDialog::validateRootFolderInput(QString &p_msg)
{
    auto rootFolderPath = m_infoWidget->getRootFolderPath();
    if (!PathUtils::isLegalPath(rootFolderPath)) {
        Utils::appendMsg(p_msg, tr("Please specify a valid root folder for the notebook."));
        return false;
    }

    {
        QFileInfo finfo(rootFolderPath);
        if (finfo.exists()) {
            if (finfo.isDir()) {
                if (!QDir(rootFolderPath).isEmpty()) {
                    Utils::appendMsg(p_msg,
                        tr("Root folder of the notebook must be empty. "
                           "If you want to import existing data, please try other operations."));
                    return false;
                }
            } else {
                Utils::appendMsg(p_msg, tr("Root folder should be a directory."));
                return false;
            }
        }
    }

    // Check if there already exists one notebook with the same root folder.
    {
        auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
        auto notebook = notebookMgr.findNotebookByRootFolderPath(rootFolderPath);
        if (notebook) {
            Utils::appendMsg(p_msg,
                tr("There already exists a notebook (%1) with the same root folder.").arg(notebook->getName()));
            return false;
        }
    }

    return true;
}

void NewNotebookDialog::acceptedButtonClicked()
{
    if (newNotebook()) {
        accept();
    }
}

bool NewNotebookDialog::newNotebook()
{
    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    auto paras = NotebookParameters::createNotebookParameters(notebookMgr,
                                                              m_infoWidget->getType(),
                                                              m_infoWidget->getName(),
                                                              m_infoWidget->getDescription(),
                                                              m_infoWidget->getRootFolderPath(),
                                                              m_infoWidget->getIcon(),
                                                              Notebook::c_defaultImageFolder,
                                                              Notebook::c_defaultAttachmentFolder,
                                                              QDateTime::currentDateTimeUtc(),
                                                              m_infoWidget->getBackend(),
                                                              m_infoWidget->getVersionController(),
                                                              m_infoWidget->getConfigMgr());
    try {
        notebookMgr.newNotebook(paras);
    } catch (Exception &p_e) {
        QString msg = tr("Failed to create notebook in (%1) (%2).")
                        .arg(m_infoWidget->getRootFolderPath(), p_e.what());
        qCritical() << msg;
        setInformationText(msg, ScrollDialog::InformationLevel::Error);
        return false;
    }

    return true;
}

void NewNotebookDialog::handleRootFolderPathChanged()
{
    auto nameEdit = m_infoWidget->getNameLineEdit();
    if (nameEdit->text().isEmpty()) {
        nameEdit->setText(PathUtils::dirName(m_infoWidget->getRootFolderPath()));
    }
}
