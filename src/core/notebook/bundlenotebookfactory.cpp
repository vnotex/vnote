#include "bundlenotebookfactory.h"

#include <QObject>
#include <QDebug>

#include <utils/pathutils.h>
#include "../exception.h"
#include "notebookconfigmgr/bundlenotebookconfigmgr.h"
#include "notebookparameters.h"
#include "bundlenotebook.h"
#include "notebookmgr.h"
#include "notebookconfigmgr/notebookconfig.h"

using namespace vnotex;

BundleNotebookFactory::BundleNotebookFactory()
{
}

QString BundleNotebookFactory::getName() const
{
    return QStringLiteral("bundle.vnotex");
}

QString BundleNotebookFactory::getDisplayName() const
{
    return QObject::tr("Bundle Notebook");
}

QString BundleNotebookFactory::getDescription() const
{
    return QObject::tr("A notebook with configuration files to track its content");
}

// Check if root folder is valid for a new notebook.
static void checkRootFolderForNewNotebook(const NotebookParameters &p_paras)
{
    if (p_paras.m_rootFolderPath.isEmpty()) {
        QString msg("no local root folder is specified");
        qCritical() << msg;
        throw Exception(Exception::Type::InvalidPath, msg);
    } else if (p_paras.m_ensureEmptyRootFolder && !PathUtils::isEmptyDir(p_paras.m_rootFolderPath)) {
        QString msg = QString("local root folder must be empty: %1 (%2)")
                             .arg(p_paras.m_rootFolderPath, PathUtils::absolutePath(p_paras.m_rootFolderPath));
        qCritical() << msg;
        throw Exception(Exception::Type::InvalidPath, msg);
    }
}

QSharedPointer<Notebook> BundleNotebookFactory::newNotebook(const NotebookParameters &p_paras)
{
    checkParameters(p_paras);

    checkRootFolderForNewNotebook(p_paras);

    p_paras.m_notebookConfigMgr->createEmptySkeleton(p_paras);

    auto notebook = QSharedPointer<BundleNotebook>::create(p_paras);
    return notebook;
}

QSharedPointer<Notebook> BundleNotebookFactory::createNotebook(const NotebookMgr &p_mgr,
                                                               const QString &p_rootFolderPath,
                                                               const QSharedPointer<INotebookBackend> &p_backend)
{
    // Read basic info about this notebook.
    auto nbConfig = BundleNotebookConfigMgr::readNotebookConfig(p_backend);
    auto paras = NotebookParameters::createNotebookParameters(p_mgr,
                                                              p_backend,
                                                              getName(),
                                                              nbConfig->m_name,
                                                              nbConfig->m_description,
                                                              p_rootFolderPath,
                                                              QIcon(),
                                                              nbConfig->m_imageFolder,
                                                              nbConfig->m_attachmentFolder,
                                                              nbConfig->m_createdTimeUtc,
                                                              nbConfig->m_versionController,
                                                              nbConfig->m_notebookConfigMgr);
    checkParameters(*paras);
    auto notebook = QSharedPointer<BundleNotebook>::create(*paras);
    return notebook;
}

void BundleNotebookFactory::checkParameters(const NotebookParameters &p_paras) const
{
    auto configMgr = dynamic_cast<BundleNotebookConfigMgr *>(p_paras.m_notebookConfigMgr.data());
    if (!configMgr) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("Invalid notebook configuration manager"));
    }
}

bool BundleNotebookFactory::checkRootFolder(const QSharedPointer<INotebookBackend> &p_backend)
{
    try {
        BundleNotebookConfigMgr::readNotebookConfig(p_backend);
    } catch (Exception &p_e) {
        Q_UNUSED(p_e);
        return false;
    }

    return true;
}
