#ifndef NODECONTENTMEDIAUTILS_H
#define NODECONTENTMEDIAUTILS_H

#include <QString>

namespace vnotex
{
    class INotebookBackend;
    class Node;

    // Utils to operate on the media files from node's content.
    class NodeContentMediaUtils
    {
    public:
        NodeContentMediaUtils() = delete;

        // Fetch media files from @p_node and copy them to dest folder.
        // @p_destFilePath: @p_node has been copied to @p_destFilePath.
        static void copyMediaFiles(const Node *p_node,
                                   INotebookBackend *p_backend,
                                   const QString &p_destFilePath);

        // @p_filePath: the file path to read the content for parse.
        static void copyMediaFiles(const QString &p_filePath,
                                   INotebookBackend *p_backend,
                                   const QString &p_destFilePath);

        static void removeMediaFiles(const Node *p_node);

        // Copy attachment folder.
        static void copyAttachment(Node *p_node,
                                   INotebookBackend *p_backend,
                                   const QString &p_destFilePath,
                                   const QString &p_destAttachmentFolderPath);

    private:
        static void copyMarkdownMediaFiles(const QString &p_content,
                                           const QString &p_basePath,
                                           INotebookBackend *p_backend,
                                           const QString &p_destFilePath);

        static void removeMarkdownMediaFiles(const Node *p_node);

        // Fix local relative internal links locating in @p_srcFolderPath.
        static void fixMarkdownLinks(const QString &p_srcFolderPath,
                                     INotebookBackend *p_backend,
                                     const QString &p_destFilePath,
                                     const QString &p_destFolderPath);
    };
}

#endif // NODECONTENTMEDIAUTILS_H
