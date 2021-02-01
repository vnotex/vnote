#include "importfolderutils.h"

#include <notebook/notebook.h>
#include <core/exception.h>
#include <QCoreApplication>
#include "legacynotebookutils.h"
#include <utils/utils.h>

using namespace vnotex;

void ImportFolderUtils::importFolderContents(Notebook *p_notebook,
                                             Node *p_node,
                                             const QStringList &p_suffixes,
                                             QString &p_errMsg)
{
    auto rootDir = p_node->toDir();
    auto children = rootDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const auto &child : children) {
        if (child.isDir()) {
            if (p_notebook->isBuiltInFolder(p_node, child.fileName())) {
                continue;
            }

            QSharedPointer<Node> node;
            try {
                node = p_notebook->addAsNode(p_node, Node::Flag::Container, child.fileName(), NodeParameters());
            } catch (Exception &p_e) {
                Utils::appendMsg(p_errMsg, ImportFolderUtilsTranslate::tr("Failed to add folder (%1) as node (%2).").arg(child.fileName(), p_e.what()));
                continue;
            }

            importFolderContents(p_notebook, node.data(), p_suffixes, p_errMsg);
        } else if (!p_notebook->isBuiltInFile(p_node, child.fileName())) {
            if (p_suffixes.contains(child.suffix())) {
                try {
                    p_notebook->addAsNode(p_node, Node::Flag::Content, child.fileName(), NodeParameters());
                } catch (Exception &p_e) {
                    Utils::appendMsg(p_errMsg, ImportFolderUtilsTranslate::tr("Failed to add file (%1) as node (%2).").arg(child.filePath(), p_e.what()));
                }
            }
        }
    }
}

void ImportFolderUtils::importFolderContentsByLegacyConfig(Notebook *p_notebook,
                                                           Node *p_node,
                                                           QString &p_errMsg)
{
    auto rootDir = p_node->toDir();

    const auto config = LegacyNotebookUtils::getFolderConfig(rootDir.absolutePath());

    // Remove the config file.
    LegacyNotebookUtils::removeFolderConfigFile(rootDir.absolutePath());

    // Folders.
    LegacyNotebookUtils::forEachFolder(config, [&rootDir, p_notebook, p_node, &p_errMsg](const QString &name) {
                if (!rootDir.exists(name)) {
                    Utils::appendMsg(p_errMsg, ImportFolderUtilsTranslate::tr("Folder (%1) does not exist.").arg(name));
                    return;
                }

                if (p_notebook->isBuiltInFolder(p_node, name)) {
                    Utils::appendMsg(p_errMsg, ImportFolderUtilsTranslate::tr("Folder (%1) conflicts with built-in folder.").arg(name));
                    return;
                }

                QSharedPointer<Node> node;
                try {
                    NodeParameters paras;
                    paras.m_createdTimeUtc = LegacyNotebookUtils::getCreatedTimeUtcOfFolder(rootDir.filePath(name));
                    node = p_notebook->addAsNode(p_node, Node::Flag::Container, name, paras);
                } catch (Exception &p_e) {
                    Utils::appendMsg(p_errMsg, ImportFolderUtilsTranslate::tr("Failed to add folder (%1) as node (%2).").arg(name, p_e.what()));
                    return;
                }

                ImportFolderUtils::importFolderContentsByLegacyConfig(p_notebook, node.data(), p_errMsg);
            });

    // Files.
    LegacyNotebookUtils::forEachFile(config, [&rootDir, p_notebook, p_node, &p_errMsg](const LegacyNotebookUtils::FileInfo &info) {
                if (!rootDir.exists(info.m_name)) {
                    Utils::appendMsg(p_errMsg, ImportFolderUtilsTranslate::tr("File (%1) does not exist.").arg(info.m_name));
                    return;
                }

                if (p_notebook->isBuiltInFile(p_node, info.m_name)) {
                    Utils::appendMsg(p_errMsg, ImportFolderUtilsTranslate::tr("File (%1) conflicts with built-in file.").arg(info.m_name));
                    return;
                }

                QSharedPointer<Node> node;
                try {
                    NodeParameters paras;
                    paras.m_createdTimeUtc = info.m_createdTimeUtc;
                    paras.m_modifiedTimeUtc = info.m_modifiedTimeUtc;
                    paras.m_attachmentFolder = info.m_attachmentFolder;
                    paras.m_tags = info.m_tags;
                    node = p_notebook->addAsNode(p_node, Node::Flag::Content, info.m_name, paras);
                } catch (Exception &p_e) {
                    Utils::appendMsg(p_errMsg, ImportFolderUtilsTranslate::tr("Failed to add file (%1) as node (%2).").arg(info.m_name, p_e.what()));
                    return;
                }
            });
}
