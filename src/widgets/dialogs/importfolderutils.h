#ifndef IMPORTFOLDERUTILS_H
#define IMPORTFOLDERUTILS_H

#include <QStringList>

namespace vnotex
{
    class Notebook;
    class Node;

    class ImportFolderUtils
    {
    public:
        ImportFolderUtils() = delete;

        // Process folder @p_node.
        // @p_node has already been added.
        static void importFolderContents(Notebook *p_notebook,
                                         Node *p_node,
                                         const QStringList &p_suffixes,
                                         QString &p_errMsg);

        // Process folder @p_node by legacy notebook config.
        // @p_node has already been added.
        static void importFolderContentsByLegacyConfig(Notebook *p_notebook,
                                                       Node *p_node,
                                                       QString &p_errMsg);
    };
}

#endif // IMPORTFOLDERUTILS_H
