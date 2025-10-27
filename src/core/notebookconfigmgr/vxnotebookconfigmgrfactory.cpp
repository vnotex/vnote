#include "vxnotebookconfigmgrfactory.h"

#include <QObject>

#include "../notebookbackend/inotebookbackend.h"
#include "vxnotebookconfigmgr.h"

using namespace vnotex;

const QString VXNotebookConfigMgrFactory::c_name = QStringLiteral("vx.vnotex");

const QString VXNotebookConfigMgrFactory::c_displayName =
    QObject::tr("VNoteX Notebook Configuration");

const QString VXNotebookConfigMgrFactory::c_description =
    QObject::tr("Built-in VNoteX notebook configuration");

VXNotebookConfigMgrFactory::VXNotebookConfigMgrFactory() {}

QString VXNotebookConfigMgrFactory::getName() const { return c_name; }

QString VXNotebookConfigMgrFactory::getDisplayName() const { return c_displayName; }

QString VXNotebookConfigMgrFactory::getDescription() const { return c_description; }

QSharedPointer<INotebookConfigMgr> VXNotebookConfigMgrFactory::createNotebookConfigMgr(
    const QSharedPointer<INotebookBackend> &p_backend) {
  return QSharedPointer<VXNotebookConfigMgr>::create(p_backend);
}
