#include "importnotebookdialog.h"

#include <QtWidgets>

#include "../widgetsfactory.h"
#include "vnotex.h"
#include "notebookmgr.h"
#include "notebook/inotebookfactory.h"
#include "notebook/notebookparameters.h"
#include "versioncontroller/iversioncontrollerfactory.h"
#include "notebookconfigmgr/inotebookconfigmgrfactory.h"
#include "notebookbackend/inotebookbackendfactory.h"
#include "configmgr.h"
#include <utils/pathutils.h>
#include <utils/utils.h>
#include "exception.h"
#include "notebookinfowidget.h"

using namespace vnotex;

ImportNotebookDialog::ImportNotebookDialog(QWidget *p_parent)
    : ScrollDialog(p_parent)
{
    setupUI();

    m_infoWidget->getRootFolderPathLineEdit()->setFocus();
}

void ImportNotebookDialog::setupUI()
{
    setupNotebookInfoWidget(this);
    setCentralWidget(m_infoWidget);

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    setButtonEnabled(QDialogButtonBox::Ok, false);

    setWindowTitle(tr("Import Notebook"));
}

void ImportNotebookDialog::setupNotebookInfoWidget(QWidget *p_parent)
{
    m_infoWidget = new NotebookInfoWidget(NotebookInfoWidget::Import, p_parent);
    connect(m_infoWidget, &NotebookInfoWidget::basicInfoEdited,
            this, &ImportNotebookDialog::validateInputs);
    connect(m_infoWidget, &NotebookInfoWidget::notebookBackendEdited,
            this, &ImportNotebookDialog::validateInputs);
}

void ImportNotebookDialog::acceptedButtonClicked()
{
    if (importNotebook()) {
        accept();
    }
}

void ImportNotebookDialog::validateInputs()
{
    bool valid = true;
    QString msg;

    valid = valid && validateRootFolderInput(msg);

    if (valid) {
        valid = createNotebookToImport(msg);
    } else {
        m_notebookToImport.clear();
    }

    m_infoWidget->setNotebook(m_notebookToImport.data());

    setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                  : ScrollDialog::InformationLevel::Error);
    setButtonEnabled(QDialogButtonBox::Ok, valid);
}

bool ImportNotebookDialog::validateRootFolderInput(QString &p_msg)
{
    auto rootFolderPath = m_infoWidget->getRootFolderPath();
    if (rootFolderPath.isEmpty() || !QFileInfo::exists(rootFolderPath)) {
        Utils::appendMsg(p_msg, tr("The root folder specified does not exist."));
        return false;
    }

    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();

    // Check if there already exists one notebook with the same root folder.
    {
        auto notebook = notebookMgr.findNotebookByRootFolderPath(rootFolderPath);
        if (notebook) {
            Utils::appendMsg(p_msg,
                tr("There already exists a notebook (%1) with the same root folder.").arg(notebook->getName()));
            return false;
        }
    }

    // Check if it is a valid bundle notebook root folder.
    {
        auto factory = notebookMgr.getBundleNotebookFactory();
        auto backendName = m_infoWidget->getBackend();
        auto backend = notebookMgr.createNotebookBackend(backendName, rootFolderPath);
        if (!factory->checkRootFolder(backend)) {
            Utils::appendMsg(p_msg,
                tr("Not a valid %1 root folder (%2).").arg(factory->getDisplayName(), rootFolderPath));
            return false;
        }

    }

    return true;
}

bool ImportNotebookDialog::createNotebookToImport(QString &p_msg)
{
    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    auto factory = notebookMgr.getBundleNotebookFactory();

    auto rootFolderPath = m_infoWidget->getRootFolderPath();

    auto backendName = m_infoWidget->getBackend();
    auto backend = notebookMgr.createNotebookBackend(backendName, rootFolderPath);

    try {
        m_notebookToImport = factory->createNotebook(notebookMgr,
                                                     rootFolderPath,
                                                     backend);
    } catch (Exception &p_e) {
        Utils::appendMsg(p_msg, tr("Failed to create notebook from root folder (%1) (%2).").arg(rootFolderPath, p_e.what()));
        return false;
    }

    Q_ASSERT(m_notebookToImport);
    return true;
}

bool ImportNotebookDialog::importNotebook()
{
    if (!m_notebookToImport) {
        QString msg = tr("Failed to import notebook.");
        qCritical() << msg;
        setInformationText(msg, ScrollDialog::InformationLevel::Error);
        return false;
    }

    try {
        auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
        notebookMgr.importNotebook(m_notebookToImport);
    } catch (Exception &p_e) {
        QString msg = tr("Failed to import notebook (%1).").arg(p_e.what());
        qCritical() << msg;
        setInformationText(msg, ScrollDialog::InformationLevel::Error);
        return false;
    }

    return true;
}
