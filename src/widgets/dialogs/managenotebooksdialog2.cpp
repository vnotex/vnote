#include "managenotebooksdialog2.h"

#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>

#include <core/servicelocator.h>
#include <core/services/notebookservice.h>

#include "../listwidget.h"
#include "../messageboxhelper.h"
#include "../propertydefs.h"
#include "../widgetsfactory.h"
#include <utils/widgetutils.h>

using namespace vnotex;

ManageNotebooksDialog2::ManageNotebooksDialog2(ServiceLocator &p_services,
                                               const QString &p_currentNotebookId,
                                               QWidget *p_parent)
    : Dialog(p_parent), m_services(p_services), m_initialNotebookId(p_currentNotebookId) {
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

  // Form for notebook properties.
  auto *formLayout = new QFormLayout();
  infoLayout->addLayout(formLayout);

  // Name input.
  m_nameEdit = WidgetsFactory::createLineEdit(infoWidget);
  m_nameEdit->setPlaceholderText(tr("Notebook name"));
  formLayout->addRow(tr("Name:"), m_nameEdit);
  connect(m_nameEdit, &QLineEdit::textChanged, this,
          [this]() { setChangesUnsaved(!m_currentNotebookId.isEmpty()); });

  // Description input.
  m_descriptionEdit = new QPlainTextEdit(infoWidget);
  m_descriptionEdit->setPlaceholderText(tr("Description"));
  m_descriptionEdit->setMaximumHeight(100);
  formLayout->addRow(tr("Description:"), m_descriptionEdit);
  connect(m_descriptionEdit, &QPlainTextEdit::textChanged, this,
          [this]() { setChangesUnsaved(!m_currentNotebookId.isEmpty()); });

  // Root folder (read-only) with Open button.
  auto *rootFolderWidget = new QWidget(infoWidget);
  auto *rootFolderLayout = new QHBoxLayout(rootFolderWidget);
  rootFolderLayout->setContentsMargins(0, 0, 0, 0);

  m_rootFolderEdit = WidgetsFactory::createLineEdit(rootFolderWidget);
  m_rootFolderEdit->setReadOnly(true);
  rootFolderLayout->addWidget(m_rootFolderEdit, 1);

  auto *openFolderBtn = new QPushButton(tr("Open"), rootFolderWidget);
  openFolderBtn->setToolTip(tr("Open root folder in file explorer"));
  rootFolderLayout->addWidget(openFolderBtn);
  connect(openFolderBtn, &QPushButton::clicked, this,
          &ManageNotebooksDialog2::openRootFolderInExplorer);

  rootFolderWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  formLayout->addRow(tr("Root Folder:"), rootFolderWidget);

  // Notebook type (read-only label).
  m_typeLabel = new QLabel(infoWidget);
  formLayout->addRow(tr("Type:"), m_typeLabel);

  // Stretch to push buttons to bottom.
  infoLayout->addStretch();

  // Action buttons.
  auto *btnLayout = new QHBoxLayout();
  infoLayout->addLayout(btnLayout);

  btnLayout->addStretch();

  m_closeBtn = new QPushButton(tr("Close Notebook"), infoWidget);
  m_closeBtn->setEnabled(false);
  btnLayout->addWidget(m_closeBtn);
  connect(m_closeBtn, &QPushButton::clicked, this,
          &ManageNotebooksDialog2::closeSelectedNotebook);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Reset |
                     QDialogButtonBox::Cancel);

  setWindowTitle(tr("Manage Notebooks"));
}

void ManageNotebooksDialog2::loadNotebooks() {
  setChangesUnsaved(false);
  m_notebookList->clear();

  auto *notebookService = m_services.get<NotebookService>();
  QJsonArray notebooks = notebookService->listNotebooks();

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
          this, [this, p_previous]() { m_notebookList->setCurrentItem(p_previous); },
          Qt::QueuedConnection);
      return;
    }
  }

  m_currentNotebookId = newId;
  selectNotebook(m_currentNotebookId);
}

void ManageNotebooksDialog2::selectNotebook(const QString &p_notebookId) {
  // Block signals while populating to avoid triggering unsaved changes.
  m_nameEdit->blockSignals(true);
  m_descriptionEdit->blockSignals(true);

  if (p_notebookId.isEmpty()) {
    m_nameEdit->clear();
    m_descriptionEdit->clear();
    m_rootFolderEdit->clear();
    m_typeLabel->clear();
    m_closeBtn->setEnabled(false);
  } else {
    auto *notebookService = m_services.get<NotebookService>();
    QJsonObject config = notebookService->getNotebookConfig(p_notebookId);

    m_nameEdit->setText(config["name"].toString());
    m_descriptionEdit->setPlainText(config["description"].toString());
    m_rootFolderEdit->setText(config["rootFolder"].toString());

    // Display user-friendly type name.
    QString type = config["type"].toString();
    if (type == QStringLiteral("bundled")) {
      m_typeLabel->setText(tr("Bundled Notebook"));
    } else if (type == QStringLiteral("raw")) {
      m_typeLabel->setText(tr("Raw Notebook"));
    } else {
      m_typeLabel->setText(type);
    }

    m_closeBtn->setEnabled(true);
  }

  m_nameEdit->blockSignals(false);
  m_descriptionEdit->blockSignals(false);

  setChangesUnsaved(false);
}

void ManageNotebooksDialog2::setChangesUnsaved(bool p_unsaved) {
  m_changesUnsaved = p_unsaved;
  setButtonEnabled(QDialogButtonBox::Apply, m_changesUnsaved);
  setButtonEnabled(QDialogButtonBox::Reset, m_changesUnsaved);
}

bool ManageNotebooksDialog2::validateInputs() {
  if (m_nameEdit->text().trimmed().isEmpty()) {
    setInformationText(tr("Please specify a name for the notebook."),
                       Dialog::InformationLevel::Error);
    return false;
  }
  return true;
}

bool ManageNotebooksDialog2::saveChangesToNotebook() {
  if (!m_changesUnsaved || m_currentNotebookId.isEmpty()) {
    return true;
  }

  if (!validateInputs()) {
    return false;
  }

  auto *notebookService = m_services.get<NotebookService>();

  // Read existing config, update only name and description, then write back.
  // vxcore does NOT support partial updates - missing fields get default values.
  // Note: rootFolder and type in the JSON are ignored by vxcore (not part of NotebookConfig).
  QJsonObject config = notebookService->getNotebookConfig(m_currentNotebookId);
  config["name"] = m_nameEdit->text().trimmed();
  config["description"] = m_descriptionEdit->toPlainText();

  QString configJson = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
  notebookService->updateNotebookConfig(m_currentNotebookId, configJson);

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

  auto *notebookService = m_services.get<NotebookService>();
  QJsonObject config = notebookService->getNotebookConfig(m_currentNotebookId);
  QString name = config["name"].toString();
  QString rootFolder = config["rootFolder"].toString();

  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Question, tr("Close notebook (%1)?").arg(name),
      tr("The notebook could be opened by VNote again later."),
      tr("Notebook location: %1").arg(rootFolder), this);

  if (ret != QMessageBox::Ok) {
    return;
  }

  notebookService->closeNotebook(m_currentNotebookId);

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

void ManageNotebooksDialog2::resetButtonClicked() {
  selectNotebook(m_currentNotebookId);
}

void ManageNotebooksDialog2::appliedButtonClicked() {
  if (saveChangesToNotebook()) {
    // Reload to show updated name in list.
    m_initialNotebookId = m_currentNotebookId;
    loadNotebooks();
  }
}

void ManageNotebooksDialog2::openRootFolderInExplorer() {
  QString rootFolder = m_rootFolderEdit->text();
  if (!rootFolder.isEmpty()) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(rootFolder));
  }
}
