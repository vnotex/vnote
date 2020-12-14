#include "importlegacynotebookdialog.h"

#include <QLineEdit>
#include <QFileInfo>

#include "notebookinfowidget.h"
#include <utils/pathutils.h>
#include <utils/utils.h>
#include <utils/fileutils.h>
#include "vnotex.h"
#include "notebookmgr.h"
#include "legacynotebookutils.h"
#include "../messageboxhelper.h"
#include <exception.h>
#include "importfolderutils.h"

using namespace vnotex;

ImportLegacyNotebookDialog::ImportLegacyNotebookDialog(QWidget *p_parent)
    : NewNotebookDialog(p_parent)
{
    setWindowTitle(tr("Import Legacy Notebook"));

    m_infoWidget->setMode(NotebookInfoWidget::Mode::CreateFromLegacy);
    m_infoWidget->getRootFolderPathLineEdit()->setFocus();
}

void ImportLegacyNotebookDialog::acceptedButtonClicked()
{
    if (isCompleted()) {
        accept();
        return;
    }

    // Warn user about the transformation.
    int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Warning,
                                                 tr("Once imported, the legacy notebook could no longer be recognized by legacy VNote!"),
                                                 QString(),
                                                 tr("Welcome to VNoteX and the new VNote!"),
                                                 this);
    if (ret == QMessageBox::Ok && importLegacyNotebook()) {
        accept();
        return;
    }
}

bool ImportLegacyNotebookDialog::validateRootFolderInput(QString &p_msg)
{
    const auto rootFolderPath = m_infoWidget->getRootFolderPath();
    if (!QFileInfo::exists(rootFolderPath) || !PathUtils::isLegalPath(rootFolderPath)) {
        Utils::appendMsg(p_msg, tr("Please specify a valid root folder to import."));
        return false;
    }

    if (!LegacyNotebookUtils::isLegacyNotebookRootFolder(rootFolderPath)) {
        Utils::appendMsg(p_msg, tr("Failed to recognize a legacy notebook from the root folder."));
        return false;
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

bool ImportLegacyNotebookDialog::importLegacyNotebook()
{
    const auto rootFolderPath = m_infoWidget->getRootFolderPath();

    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    auto imageFolder = LegacyNotebookUtils::getImageFolderOfNotebook(rootFolderPath);
    if (imageFolder.isEmpty()) {
        imageFolder = Notebook::c_defaultImageFolder;
    }
    auto attachmentFolder = LegacyNotebookUtils::getAttachmentFolderOfNotebook(rootFolderPath);
    if (attachmentFolder.isEmpty()) {
        attachmentFolder = Notebook::c_defaultAttachmentFolder;
    }
    auto paras = NotebookParameters::createNotebookParameters(notebookMgr,
                                                              m_infoWidget->getType(),
                                                              m_infoWidget->getName(),
                                                              m_infoWidget->getDescription(),
                                                              rootFolderPath,
                                                              m_infoWidget->getIcon(),
                                                              imageFolder,
                                                              attachmentFolder,
                                                              LegacyNotebookUtils::getCreatedTimeUtcOfFolder(rootFolderPath),
                                                              m_infoWidget->getBackend(),
                                                              m_infoWidget->getVersionController(),
                                                              m_infoWidget->getConfigMgr());
    paras->m_ensureEmptyRootFolder = false;
    QSharedPointer<Notebook> nb;
    try {
        nb = notebookMgr.newNotebook(paras);
    } catch (Exception &p_e) {
        QString msg = tr("Failed to create notebook in %1 (%2).").arg(rootFolderPath, p_e.what());
        qCritical() << msg;
        setInformationText(msg, ScrollDialog::InformationLevel::Error);
        return false;
    }

    QString errMsg;

    // Copy legacy recycle bin folder into new one or just delete it if it is empty.
    {
        auto legacyBinFolder = LegacyNotebookUtils::getRecycleBinFolderOfNotebook(rootFolderPath);
        if (!legacyBinFolder.isEmpty()) {
            auto binFolderPath = PathUtils::concatenateFilePath(rootFolderPath, legacyBinFolder);
            if (PathUtils::isEmptyDir(binFolderPath)) {
                FileUtils::removeDir(binFolderPath);
            } else {
                nb->moveDirToRecycleBin(binFolderPath);
            }
        }
    }

    auto rootNode = nb->getRootNode();
    ImportFolderUtils::importFolderContentsByLegacyConfig(nb.data(), rootNode.data(), errMsg);

    emit nb->nodeUpdated(rootNode.data());

    if (!errMsg.isEmpty()) {
        qWarning() << errMsg;
        setInformationText(errMsg, ScrollDialog::InformationLevel::Error);
        completeButStay();
        return false;
    }

    return true;
}
