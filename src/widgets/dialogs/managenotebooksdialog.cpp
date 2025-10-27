#include "managenotebooksdialog.h"

#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "../listwidget.h"
#include "../messageboxhelper.h"
#include "../propertydefs.h"
#include "../widgetsfactory.h"
#include "exception.h"
#include "notebook/notebook.h"
#include "notebookinfowidget.h"
#include "notebookmgr.h"
#include "vnotex.h"
#include <core/configmgr.h>
#include <utils/iconutils.h>
#include <utils/utils.h>
#include <utils/widgetutils.h>

using namespace vnotex;

ManageNotebooksDialog::ManageNotebooksDialog(const Notebook *p_notebook, QWidget *p_parent)
    : Dialog(p_parent) {
  setupUI();

  loadNotebooks(p_notebook);
}

void ManageNotebooksDialog::setupUI() {
  auto widget = new QWidget(this);
  setCentralWidget(widget);

  auto mainLayout = new QHBoxLayout(widget);

  setupNotebookList(widget);
  mainLayout->addWidget(m_notebookList);
  mainLayout->setStretchFactor(m_notebookList, 1);

  {
    m_infoScrollArea = new QScrollArea(widget);
    m_infoScrollArea->setWidgetResizable(true);
    mainLayout->addWidget(m_infoScrollArea);
    mainLayout->setStretchFactor(m_infoScrollArea, 3);

    auto infoWidget = new QWidget(widget);
    m_infoScrollArea->setWidget(infoWidget);

    auto infoLayout = new QVBoxLayout(infoWidget);

    setupNotebookInfoWidget(infoWidget);
    infoLayout->addWidget(m_notebookInfoWidget);

    auto btnLayout = new QHBoxLayout();
    infoLayout->addLayout(btnLayout);

    m_closeNotebookBtn = new QPushButton(tr("Close Noteboook"), infoWidget);
    btnLayout->addStretch();
    btnLayout->addWidget(m_closeNotebookBtn);
    connect(m_closeNotebookBtn, &QPushButton::clicked, this, [this]() {
      if (checkUnsavedChanges()) {
        return;
      }
      closeNotebook(m_notebookInfoWidget->getNotebook());
    });

    m_deleteNotebookBtn = new QPushButton(tr("Delete"), infoWidget);
    WidgetUtils::setPropertyDynamically(m_deleteNotebookBtn, PropertyDefs::c_dangerButton, true);
    btnLayout->addWidget(m_deleteNotebookBtn);
    connect(m_deleteNotebookBtn, &QPushButton::clicked, this, [this]() {
      if (checkUnsavedChanges()) {
        return;
      }
      removeNotebook(m_notebookInfoWidget->getNotebook());
    });
  }

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Reset |
                     QDialogButtonBox::Cancel);

  setWindowTitle(tr("Manage Notebooks"));
}

Notebook *ManageNotebooksDialog::getNotebookFromItem(const QListWidgetItem *p_item) const {
  Notebook *notebook = nullptr;
  if (p_item) {
    auto id = static_cast<ID>(p_item->data(Qt::UserRole).toULongLong());
    notebook = VNoteX::getInst().getNotebookMgr().findNotebookById(id).data();
  }

  return notebook;
}

void ManageNotebooksDialog::setupNotebookList(QWidget *p_parent) {
  m_notebookList = new ListWidget(p_parent);
  connect(m_notebookList, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *p_item, QListWidgetItem *p_previous) {
            auto notebook = getNotebookFromItem(p_item);
            if (m_changesUnsaved) {
              // Have unsaved changes.
              if (m_notebook != notebook) {
                checkUnsavedChanges();

                QTimer::singleShot(
                    50, this, [this, p_previous]() { m_notebookList->setCurrentItem(p_previous); });
              }

              return;
            }

            m_notebook = notebook;
            selectNotebook(m_notebook);
          });
}

void ManageNotebooksDialog::setupNotebookInfoWidget(QWidget *p_parent) {
  m_notebookInfoWidget = new NotebookInfoWidget(NotebookInfoWidget::Edit, p_parent);
  connect(m_notebookInfoWidget, &NotebookInfoWidget::basicInfoEdited, this, [this]() {
    bool unsaved = false;
    if (m_notebook) {
      if (m_notebook->getName() != m_notebookInfoWidget->getName() ||
          m_notebook->getDescription() != m_notebookInfoWidget->getDescription()) {
        unsaved = true;
      }
    }

    setChangesUnsaved(unsaved);
  });
}

void ManageNotebooksDialog::loadNotebooks(const Notebook *p_notebook) {
  setChangesUnsaved(false);

  m_notebookList->clear();

  bool hasCurrentItem = false;
  auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
  const auto &notebooks = notebookMgr.getNotebooks();
  for (auto &nb : notebooks) {
    auto item = new QListWidgetItem(nb->getName());
    item->setData(Qt::UserRole, nb->getId());
    item->setToolTip(nb->getName());
    m_notebookList->addItem(item);

    if (nb.data() == p_notebook) {
      hasCurrentItem = true;
      m_notebookList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    }
  }

  if (!hasCurrentItem) {
    if (notebooks.isEmpty()) {
      selectNotebook(nullptr);
    } else {
      m_notebookList->setCurrentRow(0);
    }
  }
}

void ManageNotebooksDialog::selectNotebook(Notebook *p_notebook) {
  m_notebookInfoWidget->setNotebook(p_notebook);
  setChangesUnsaved(false);

  // Update buttons.
  m_closeNotebookBtn->setEnabled(p_notebook);
  m_deleteNotebookBtn->setEnabled(p_notebook);

  WidgetUtils::resizeToHideScrollBarLater(m_infoScrollArea, false, true);
}

void ManageNotebooksDialog::acceptedButtonClicked() {
  if (saveChangesToNotebook()) {
    accept();
  }
}

void ManageNotebooksDialog::resetButtonClicked() {
  m_notebookInfoWidget->clear();
  selectNotebook(m_notebook);
}

void ManageNotebooksDialog::appliedButtonClicked() {
  Q_ASSERT(m_changesUnsaved);
  if (saveChangesToNotebook()) {
    loadNotebooks(m_notebook);
  }
}

void ManageNotebooksDialog::setChangesUnsaved(bool p_unsaved) {
  m_changesUnsaved = p_unsaved;
  setButtonEnabled(QDialogButtonBox::Apply, m_changesUnsaved);
  setButtonEnabled(QDialogButtonBox::Reset, m_changesUnsaved);
}

bool ManageNotebooksDialog::validateInputs() {
  bool valid = true;
  QString msg;

  valid = valid && validateNameInput(msg);

  setInformationText(msg, valid ? Dialog::InformationLevel::Info : Dialog::InformationLevel::Error);
  return valid;
}

bool ManageNotebooksDialog::validateNameInput(QString &p_msg) {
  if (m_notebookInfoWidget->getName().isEmpty()) {
    Utils::appendMsg(p_msg, tr("Please specify a name for the notebook."));
    return false;
  }

  return true;
}

bool ManageNotebooksDialog::saveChangesToNotebook() {
  if (!m_changesUnsaved || !m_notebook) {
    return true;
  }

  if (!validateInputs()) {
    return false;
  }

  m_notebook->updateName(m_notebookInfoWidget->getName());
  m_notebook->updateDescription(m_notebookInfoWidget->getDescription());
  return true;
}

bool ManageNotebooksDialog::closeNotebook(const Notebook *p_notebook) {
  if (!p_notebook) {
    return false;
  }

  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Question, tr("Close notebook (%1)?").arg(p_notebook->getName()),
      tr("The notebook could be opened by VNote again via \"Open Other Notebooks\" operation."),
      tr("Notebook location: %1").arg(p_notebook->getRootFolderAbsolutePath()), this);
  if (ret != QMessageBox::Ok) {
    return false;
  }

  try {
    VNoteX::getInst().getNotebookMgr().closeNotebook(p_notebook->getId());
  } catch (Exception &p_e) {
    MessageBoxHelper::notify(MessageBoxHelper::Warning,
                             tr("Failed to close notebook (%1)").arg(p_e.what()), this);
  }

  loadNotebooks(nullptr);
  return true;
}

void ManageNotebooksDialog::removeNotebook(const Notebook *p_notebook) {
  if (!p_notebook) {
    return;
  }

  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Warning,
      tr("Please close the notebook in VNote first and delete the notebook root folder files "
         "manually."),
      tr("Press \"Ok\" to close the notebook and open the location of the notebook root folder."),
      tr("Notebook location: %1").arg(p_notebook->getRootFolderAbsolutePath()), this);
  if (ret != QMessageBox::Ok) {
    return;
  }

  const auto rootFolder = p_notebook->getRootFolderAbsolutePath();

  if (closeNotebook(p_notebook)) {
    WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(rootFolder));
  }
}

bool ManageNotebooksDialog::checkUnsavedChanges() {
  if (m_changesUnsaved) {
    MessageBoxHelper::notify(MessageBoxHelper::Warning,
                             tr("There are unsaved changes to current notebook."), this);
    return true;
  }

  return false;
}
