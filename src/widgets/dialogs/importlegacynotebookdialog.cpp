#include "importlegacynotebookdialog.h"

#include <QFileInfo>
#include <QLineEdit>

#include "../messageboxhelper.h"
#include "importfolderutils.h"
#include "legacynotebookutils.h"
#include "notebookinfowidget.h"
#include "notebookmgr.h"
#include "vnotex.h"
#include <exception.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <utils/utils.h>

using namespace vnotex;

ImportLegacyNotebookDialog::ImportLegacyNotebookDialog(QWidget *p_parent)
    : NewNotebookDialog(p_parent) {
  setWindowTitle(tr("Open Legacy Notebook"));

  m_infoWidget->setMode(NotebookInfoWidget::Mode::CreateFromLegacy);
  m_infoWidget->getRootFolderPathLineEdit()->setFocus();
}

void ImportLegacyNotebookDialog::acceptedButtonClicked() {
  if (isCompleted()) {
    accept();
    return;
  }

  // Warn user about the transformation.
  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Warning,
      tr("Once opened, the legacy notebook could no longer be recognized by legacy VNote!"),
      QString(), tr("Welcome to VNoteX and the new VNote!"), this);
  if (ret == QMessageBox::Ok && importLegacyNotebook()) {
    accept();
    return;
  }
}

bool ImportLegacyNotebookDialog::validateRootFolderInput(QString &p_msg) {
  const auto rootFolderPath = m_infoWidget->getRootFolderPath();
  if (!QFileInfo::exists(rootFolderPath) || !PathUtils::isLegalPath(rootFolderPath)) {
    Utils::appendMsg(p_msg, tr("Please specify a valid root folder to open."));
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
      Utils::appendMsg(p_msg, tr("There already exists a notebook (%1) with the same root folder.")
                                  .arg(notebook->getName()));
      return false;
    }
  }

  return true;
}

bool ImportLegacyNotebookDialog::importLegacyNotebook() {
  const auto rootFolderPath = m_infoWidget->getRootFolderPath();

  auto &notebookMgr = VNoteX::getInst().getNotebookMgr();

  QString imageFolder;
  QString attachmentFolder;
  QDateTime createdTimeUtc;

  try {
    imageFolder = LegacyNotebookUtils::getImageFolderOfNotebook(rootFolderPath);
    if (imageFolder.isEmpty()) {
      imageFolder = Notebook::c_defaultImageFolder;
    }

    attachmentFolder = LegacyNotebookUtils::getAttachmentFolderOfNotebook(rootFolderPath);
    if (attachmentFolder.isEmpty()) {
      attachmentFolder = Notebook::c_defaultAttachmentFolder;
    }

    createdTimeUtc = LegacyNotebookUtils::getCreatedTimeUtcOfFolder(rootFolderPath);
  } catch (Exception &p_e) {
    QString msg = tr("Failed to read legacy notebook configuration in (%1) (%2).")
                      .arg(rootFolderPath, p_e.what());
    qCritical() << msg;
    setInformationText(msg, ScrollDialog::InformationLevel::Error);
    return false;
  }

  auto paras = NotebookParameters::createNotebookParameters(
      notebookMgr, m_infoWidget->getType(), m_infoWidget->getName(), m_infoWidget->getDescription(),
      rootFolderPath, m_infoWidget->getIcon(), imageFolder, attachmentFolder, createdTimeUtc,
      m_infoWidget->getBackend(), m_infoWidget->getVersionController(),
      m_infoWidget->getConfigMgr());
  paras->m_ensureEmptyRootFolder = false;
  QSharedPointer<Notebook> nb;
  try {
    nb = notebookMgr.newNotebook(paras);
  } catch (Exception &p_e) {
    QString msg = tr("Failed to create notebook in (%1) (%2).").arg(rootFolderPath, p_e.what());
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

  try {
    ImportFolderUtils::importFolderContentsByLegacyConfig(nb.data(), rootNode.data(), errMsg);
  } catch (Exception &p_e) {
    errMsg = tr("Failed to import folder contents by legacy config in (%1) (%2).")
                 .arg(rootFolderPath, p_e.what());
  }

  emit nb->nodeUpdated(rootNode.data());

  if (!errMsg.isEmpty()) {
    qWarning() << errMsg;
    setInformationText(errMsg, ScrollDialog::InformationLevel::Error);
    completeButStay();
    return false;
  }

  return true;
}
