#ifndef FILETYPEHELPER_H
#define FILETYPEHELPER_H

#include <QString>
#include <QMap>
#include <QVector>

namespace vnotex
{
    class FileType
    {
    public:
        // There may be other types after Others.
        enum Type
        {
            Markdown = 0,
            Text,
            Others
        };

        // Type.
        int m_type = -1;

        QString m_typeName;

        QString m_displayName;

        QStringList m_suffixes;

        QString preferredSuffix() const;

        bool isMarkdown() const;
    };

    class FileTypeHelper
    {
    public:
        const FileType &getFileType(const QString &p_filePath) const;

        const FileType &getFileType(int p_type) const;

        const FileType &getFileTypeByName(const QString &p_typeName) const;

        const FileType &getFileTypeBySuffix(const QString &p_suffix) const;

        const QVector<FileType> &getAllFileTypes() const;

        bool checkFileType(const QString &p_filePath, int p_type) const;

        static const FileTypeHelper &getInst();

    private:
        FileTypeHelper();

        void setupBuiltInTypes();

        void setupSuffixTypeMap();

        // Built-in Type could be accessed via enum Type.
        QVector<FileType> m_fileTypes;

        // suffix -> index of m_fileTypes.
        // TODO: handle suffix conflicts.
        QMap<QString, int> m_suffixTypeMap;
    };
} // ns vnotex

#endif // FILETYPEHELPER_H
