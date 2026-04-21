#include "nodepropertiesdialog2.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>

#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

NodePropertiesDialog2::NodePropertiesDialog2(ServiceLocator &p_services,
                                             const NodeIdentifier &p_nodeId,
                                             const NodeInfo &p_nodeInfo, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services) {
  setupUI(p_nodeId, p_nodeInfo);

  setWindowTitle(tr("Properties - %1").arg(p_nodeInfo.name));
}

void NodePropertiesDialog2::setupUI(const NodeIdentifier &p_nodeId, const NodeInfo &p_nodeInfo) {
  auto *mainWidget = new QWidget(this);
  auto *mainLayout = WidgetsFactory::createFormLayout(mainWidget);

  // Name.
  auto *nameLabel = new QLabel(p_nodeInfo.name, mainWidget);
  mainLayout->addRow(tr("Name:"), nameLabel);

  // Path.
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  QString absolutePath = notebookSvc->buildAbsolutePath(p_nodeId.notebookId, p_nodeId.relativePath);
  if (absolutePath.isEmpty()) {
    absolutePath = p_nodeId.relativePath;
  }
  auto *pathLineEdit = WidgetsFactory::createLineEdit(mainWidget);
  pathLineEdit->setReadOnly(true);
  pathLineEdit->setText(absolutePath);
  mainLayout->addRow(tr("Path:"), pathLineEdit);

  // Type.
  auto *typeLabel = new QLabel(p_nodeInfo.isFolder ? tr("Folder") : tr("Note"), mainWidget);
  mainLayout->addRow(tr("Type:"), typeLabel);

  // Created.
  auto *createdLabel =
      new QLabel(p_nodeInfo.createdTimeUtc.toLocalTime().toString(), mainWidget);
  mainLayout->addRow(tr("Created:"), createdLabel);

  // Modified.
  auto *modifiedLabel =
      new QLabel(p_nodeInfo.modifiedTimeUtc.toLocalTime().toString(), mainWidget);
  mainLayout->addRow(tr("Modified:"), modifiedLabel);

  // Children (folders only).
  if (p_nodeInfo.isFolder) {
    auto *childrenLabel = new QLabel(QString::number(p_nodeInfo.childCount), mainWidget);
    mainLayout->addRow(tr("Children:"), childrenLabel);
  }

  // Tags (files with tags only).
  if (!p_nodeInfo.tags.isEmpty()) {
    auto *tagsLabel = new QLabel(p_nodeInfo.tags.join(QStringLiteral(", ")), mainWidget);
    mainLayout->addRow(tr("Tags:"), tagsLabel);
  }

  // UUID.
  QString uuid;
  QJsonObject fileInfo = notebookSvc->getFileInfo(p_nodeId.notebookId, p_nodeId.relativePath);
  if (!fileInfo.isEmpty() && fileInfo.contains(QStringLiteral("id"))) {
    uuid = fileInfo[QStringLiteral("id")].toString();
  }
  auto *uuidLineEdit = WidgetsFactory::createLineEdit(mainWidget);
  uuidLineEdit->setReadOnly(true);
  uuidLineEdit->setText(uuid.isEmpty() ? tr("N/A") : uuid);
  mainLayout->addRow(tr("UUID:"), uuidLineEdit);

  setCentralWidget(mainWidget);

  setDialogButtonBox(QDialogButtonBox::Ok);
}
