#include "newnotebookfromfolderdialog.h"

#include <QGroupBox>
#include <QLineEdit>
#include <QVBoxLayout>

#include "../widgetsfactory.h"
#include "exception.h"
#include "folderfilesfilterwidget.h"
#include "importfolderutils.h"
#include "notebookinfowidget.h"
#include "notebookmgr.h"
#include "vnotex.h"
#include <notebook/inotebookfactory.h>
#include <notebook/notebook.h>
#include <notebook/notebookparameters.h>
#include <utils/pathutils.h>
#include <utils/utils.h>

using namespace vnotex;

NewNotebookFromFolderDialog::NewNotebookFromFolderDialog(QWidget *p_parent)
    : ScrollDialog(p_parent) {
  setupUI();

  m_filterWidget->getFolderPathEdit()->setFocus();
}

void NewNotebookFromFolderDialog::setupUI() {
  auto widget = new QWidget(this);
  auto mainLayout = new QVBoxLayout(widget);
  setCentralWidget(widget);

  {
    auto box = new QGroupBox(tr("Source Folder"), widget);
    auto layout = new QVBoxLayout(box);
    setupFolderFilesFilterWidget(widget);
    layout->addWidget(m_filterWidget);

    mainLayout->addWidget(box);
  }

  setupNotebookInfoWidget(widget);
  mainLayout->addWidget(m_infoWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  setButtonEnabled(QDialogButtonBox::Ok, false);

  setWindowTitle(tr("New Notebook From Folder"));
}

void NewNotebookFromFolderDialog::setupFolderFilesFilterWidget(QWidget *p_parent) {
  m_filterWidget = new FolderFilesFilterWidget(p_parent);
  connect(m_filterWidget, &FolderFilesFilterWidget::filesChanged, this, [this]() {
    m_infoWidget->setRootFolderPath(m_filterWidget->getFolderPath());
    validateInputs();
  });
}

void NewNotebookFromFolderDialog::setupNotebookInfoWidget(QWidget *p_parent) {
  m_infoWidget = new NotebookInfoWidget(NotebookInfoWidget::CreateFromFolder, p_parent);
  connect(m_infoWidget, &NotebookInfoWidget::basicInfoEdited, this,
          &NewNotebookFromFolderDialog::validateInputs);
}

void NewNotebookFromFolderDialog::validateInputs() {
  bool valid = true;
  QString msg;

  valid = valid && validateNameInput(msg);
  valid = valid && validateRootFolderInput(msg);

  setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                : ScrollDialog::InformationLevel::Error);
  setButtonEnabled(QDialogButtonBox::Ok, valid);
}

bool NewNotebookFromFolderDialog::validateNameInput(QString &p_msg) {
  p_msg.clear();

  if (m_infoWidget->getName().isEmpty()) {
    Utils::appendMsg(p_msg, tr("Please specify a name for the notebook."));
    return false;
  }

  return true;
}

bool NewNotebookFromFolderDialog::validateRootFolderInput(QString &p_msg) {
  auto rootFolderPath = m_infoWidget->getRootFolderPath();
  if (!QFileInfo::exists(rootFolderPath) || !PathUtils::isLegalPath(rootFolderPath)) {
    Utils::appendMsg(p_msg, tr("Please specify a valid folder for the new notebook."));
    return false;
  }

  auto &notebookMgr = VNoteX::getInst().getNotebookMgr();

  // Check if there already exists one notebook with the same root folder.
  {
    auto notebook = notebookMgr.findNotebookByRootFolderPath(rootFolderPath);
    if (notebook) {
      Utils::appendMsg(p_msg, tr("There already exists a notebook (%1) with the same root folder.")
                                  .arg(notebook->getName()));
      return false;
    }
  }

  // Warn if it is a valid bundle notebook root folder.
  {
    auto factory = notebookMgr.getBundleNotebookFactory();
    auto backend =
        notebookMgr.createNotebookBackend(QStringLiteral("local.vnotex"), rootFolderPath);
    if (factory->checkRootFolder(backend)) {
      Utils::appendMsg(p_msg,
                       tr("The folder is likely to be the root folder of a valid bundle notebook. "
                          "You may want to use \"Open Other Notebooks\" to open it. "
                          "If continue, all existing information of the notebook may be lost."));
    }
  }

  return true;
}

void NewNotebookFromFolderDialog::acceptedButtonClicked() {
  if (isCompleted() || newNotebookFromFolder()) {
    accept();
  }
}

bool NewNotebookFromFolderDialog::newNotebookFromFolder() {
  const auto rootFolderPath = m_infoWidget->getRootFolderPath();
  auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
  auto paras = NotebookParameters::createNotebookParameters(
      notebookMgr, m_infoWidget->getType(), m_infoWidget->getName(), m_infoWidget->getDescription(),
      rootFolderPath, m_infoWidget->getIcon(), Notebook::c_defaultImageFolder,
      Notebook::c_defaultAttachmentFolder, QDateTime::currentDateTimeUtc(),
      m_infoWidget->getBackend(), m_infoWidget->getVersionController(),
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
  auto rootNode = nb->getRootNode();
  ImportFolderUtils::importFolderContents(nb.data(), rootNode.data(), m_filterWidget->getSuffixes(),
                                          errMsg);

  emit nb->nodeUpdated(rootNode.data());

  if (!errMsg.isEmpty()) {
    qWarning() << errMsg;
    setInformationText(errMsg, ScrollDialog::InformationLevel::Error);
    completeButStay();
    return false;
  }

  return true;
}
