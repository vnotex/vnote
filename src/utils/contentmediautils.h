#ifndef CONTENTMEDIAUTILS_H
#define CONTENTMEDIAUTILS_H

#include <QString>

namespace vnotex
{
    class INotebookBackend;
    class Node;
    class File;

    // Utils to operate on the media files from node's content.
    class ContentMediaUtils
    {
    public:
        ContentMediaUtils() = delete;

        // Fetch media files from @p_node and copy them to dest folder.
        // @p_destFilePath: @p_node has been copied to @p_destFilePath.
        static void copyMediaFiles(Node *p_node,
                                   INotebookBackend *p_backend,
                                   const QString &p_destFilePath);

        // @p_filePath: the file path to read the content for parse.
        static void copyMediaFiles(const QString &p_filePath,
                                   INotebookBackend *p_backend,
                                   const QString &p_destFilePath);

        static void copyMediaFiles(const File *p_file,
                                   const QString &p_destFilePath);

        static void removeMediaFiles(Node *p_node);

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

        static void removeMarkdownMediaFiles(const File *p_file, INotebookBackend *p_backend);

        // Fix local relative internal links locating in @p_srcFolderPath.
        static void fixMarkdownLinks(const QString &p_srcFolderPath,
                                     INotebookBackend *p_backend,
                                     const QString &p_destFilePath,
                                     const QString &p_destFolderPath);
    };
}

#endif // CONTENTMEDIAUTILS_H
