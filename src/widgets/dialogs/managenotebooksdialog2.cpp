#include "managenotebooksdialog2.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <controllers/managenotebookscontroller.h>
#include <core/servicelocator.h>

#include "../listwidget.h"
#include "../messageboxhelper.h"
#include "notebookinfowidget.h"

using namespace vnotex;

ManageNotebooksDialog2::ManageNotebooksDialog2(ServiceLocator &p_services,
                                               const QString &p_currentNotebookId,
                                               QWidget *p_parent)
    : Dialog(p_parent), m_services(p_services), m_initialNotebookId(p_currentNotebookId) {
  m_controller = new ManageNotebooksController(m_services, this);
  setupUI();
  loadNotebooks();
}

void ManageNotebooksDialog2::setupUI() {
  auto *widget = new QWidget(this);
  setCentralWidget(widget);

  auto *mainLayout = new QHBoxLayout(widget);

  // Left: Notebook list.
  m_notebookList = new ListWidget(widget);
  mainLayout->addWidget(m_notebookList);
  mainLayout->setStretchFactor(m_notebookList, 1);
  connect(m_notebookList, &QListWidget::currentItemChanged, this,
          &ManageNotebooksDialog2::onCurrentNotebookChanged);

  // Right: Info panel with form layout.
  auto *infoWidget = new QWidget(widget);
  mainLayout->addWidget(infoWidget);
  mainLayout->setStretchFactor(infoWidget, 3);

  auto *infoLayout = new QVBoxLayout(infoWidget);

  m_infoWidget = new NotebookInfoWidget(m_services, NotebookInfoWidget::Mode::Edit, infoWidget);
  infoLayout->addWidget(m_infoWidget);
  connect(m_infoWidget, &NotebookInfoWidget::inputEdited, this,
          [this]() { setChangesUnsaved(!m_currentNotebookId.isEmpty()); });

  // Stretch to push buttons to bottom.
  infoLayout->addStretch();

  // Action buttons.
  auto *btnLayout = new QHBoxLayout();
  infoLayout->addLayout(btnLayout);

  btnLayout->addStretch();

  m_closeBtn = new QPushButton(tr("Close Notebook"), infoWidget);
  m_closeBtn->setEnabled(false);
  btnLayout->addWidget(m_closeBtn);
  connect(m_closeBtn, &QPushButton::clicked, this, &ManageNotebooksDialog2::closeSelectedNotebook);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Reset |
                     QDialogButtonBox::Cancel);

  setWindowTitle(tr("Manage Notebooks"));
}

void ManageNotebooksDialog2::loadNotebooks() {
  setChangesUnsaved(false);
  m_notebookList->clear();

  QJsonArray notebooks = m_controller->listNotebooks();

  bool hasSelection = false;
  for (const QJsonValue &val : notebooks) {
    QJsonObject nb = val.toObject();
    QString id = nb["id"].toString();
    QString name = nb["name"].toString();

    auto *item = new QListWidgetItem(name);
    item->setData(Qt::UserRole, id);
    item->setToolTip(name);
    m_notebookList->addItem(item);

    if (id == m_initialNotebookId) {
      hasSelection = true;
      m_notebookList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    }
  }

  if (!hasSelection) {
    if (m_notebookList->count() == 0) {
      selectNotebook(QString());
    } else {
      m_notebookList->setCurrentRow(0);
    }
  }
}

void ManageNotebooksDialog2::onCurrentNotebookChanged(QListWidgetItem *p_current,
                                                      QListWidgetItem *p_previous) {
  QString newId = getNotebookIdFromItem(p_current);

  if (m_changesUnsaved && newId != m_currentNotebookId) {
    // Revert selection if there are unsaved changes.
    if (checkUnsavedChanges()) {
      QMetaObject::invokeMethod(
          this,
          [this, p_previous]() {
            const QSignalBlocker blocker(m_notebookList);
            m_notebookList->setCurrentItem(p_previous);
          },
          Qt::QueuedConnection);
      return;
    }
  }

  m_currentNotebookId = newId;
  selectNotebook(m_currentNotebookId);
}

void ManageNotebooksDialog2::selectNotebook(const QString &p_notebookId) {
  m_infoWidget->setNotebookInfo(m_controller->getNotebookInfo(p_notebookId));
  m_closeBtn->setEnabled(!p_notebookId.isEmpty());
  setChangesUnsaved(false);
}

void ManageNotebooksDialog2::setChangesUnsaved(bool p_unsaved) {
  m_changesUnsaved = p_unsaved;
  setButtonEnabled(QDialogButtonBox::Apply, m_changesUnsaved);
  setButtonEnabled(QDialogButtonBox::Reset, m_changesUnsaved);
}

bool ManageNotebooksDialog2::saveChangesToNotebook() {
  if (!m_changesUnsaved || m_currentNotebookId.isEmpty()) {
    return true;
  }

  NotebookUpdateInput input;
  input.notebookId = m_currentNotebookId;
  input.name = m_infoWidget->getName();
  input.description = m_infoWidget->getDescription();
  input.assetsFolder = m_infoWidget->getAssetsFolder();
  input.recycleBinFolder = m_infoWidget->getRecycleBinFolder();
  input.lineEnding = m_infoWidget->getLineEnding();

  NotebookOperationResult result = m_controller->updateNotebook(input);

  if (!result.success) {
    setInformationText(result.errorMessage, Dialog::InformationLevel::Error);
    return false;
  }

  setChangesUnsaved(false);
  return true;
}

bool ManageNotebooksDialog2::checkUnsavedChanges() {
  if (m_changesUnsaved) {
    MessageBoxHelper::notify(MessageBoxHelper::Warning,
                             tr("There are unsaved changes to current notebook."), this);
    return true;
  }
  return false;
}

void ManageNotebooksDialog2::closeSelectedNotebook() {
  if (checkUnsavedChanges()) {
    return;
  }

  if (m_currentNotebookId.isEmpty()) {
    return;
  }

  NotebookInfo info = m_controller->getNotebookInfo(m_currentNotebookId);

  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Question, tr("Close notebook (%1)?").arg(info.name),
      tr("The notebook could be opened by VNote again later."),
      tr("Notebook location: %1").arg(info.rootFolder), this);

  if (ret != QMessageBox::Ok) {
    return;
  }

  NotebookOperationResult result = m_controller->closeNotebook(m_currentNotebookId);

  if (!result.success) {
    setInformationText(result.errorMessage, Dialog::InformationLevel::Error);
    return;
  }

  // Reload list.
  m_initialNotebookId.clear();
  loadNotebooks();
}

QString ManageNotebooksDialog2::getNotebookIdFromItem(const QListWidgetItem *p_item) const {
  if (p_item) {
    return p_item->data(Qt::UserRole).toString();
  }
  return QString();
}

void ManageNotebooksDialog2::acceptedButtonClicked() {
  if (saveChangesToNotebook()) {
    accept();
  }
}

void ManageNotebooksDialog2::resetButtonClicked() { selectNotebook(m_currentNotebookId); }

void ManageNotebooksDialog2::appliedButtonClicked() {
  if (saveChangesToNotebook()) {
    // Reload to show updated name in list.
    m_initialNotebookId = m_currentNotebookId;
    loadNotebooks();
  }
}
