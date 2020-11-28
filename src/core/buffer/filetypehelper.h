#ifndef FILETYPEHELPER_H
#define FILETYPEHELPER_H

#include <QString>
#include <QMap>
#include <QSharedPointer>

namespace vnotex
{
    typedef QString FileType;

    // Map file suffix to file type.
    class FileTypeHelper
    {
    public:
        FileTypeHelper() = delete;

        static FileType fileType(const QString &p_filePath);

        static const FileType s_markdownFileType;

        static const FileType s_textFileType;

        static const FileType s_unknownFileType;

    private:
        static void init();

        static QSharedPointer<QMap<QString, FileType>> s_fileTypeMap;
    };
} // ns vnotex

#endif // FILETYPEHELPER_H
