#include "filetypehelper.h"

#include <QFileInfo>
#include <QDebug>

#include <utils/fileutils.h>
#include "buffer.h"

using namespace vnotex;

QString FileType::preferredSuffix() const
{
    return m_suffixes.isEmpty() ? QString() : m_suffixes.first();
}

bool FileType::isMarkdown() const
{
    return m_type == Type::Markdown;
}

FileTypeHelper::FileTypeHelper()
{
    setupBuiltInTypes();

    // TODO: read configuration file.

    setupSuffixTypeMap();
}

void FileTypeHelper::setupBuiltInTypes()
{
    {
        FileType type;
        type.m_type = FileType::Markdown;
        type.m_displayName = Buffer::tr("Markdown");
        type.m_typeName = QStringLiteral("Markdown");
        type.m_suffixes << QStringLiteral("md")
                        << QStringLiteral("mkd")
                        << QStringLiteral("rmd")
                        << QStringLiteral("markdown");
        m_fileTypes.push_back(type);
    }

    {
        FileType type;
        type.m_type = FileType::Text;
        type.m_typeName = QStringLiteral("Text");
        type.m_displayName = Buffer::tr("Text");
        type.m_suffixes << QStringLiteral("txt") << QStringLiteral("text") << QStringLiteral("log");
        m_fileTypes.push_back(type);
    }

    {
        FileType type;
        type.m_type = FileType::Others;
        type.m_typeName = QStringLiteral("Others");
        type.m_displayName = Buffer::tr("Others");
        m_fileTypes.push_back(type);
    }
}

const FileType &FileTypeHelper::getFileType(const QString &p_filePath) const
{
    Q_ASSERT(!p_filePath.isEmpty());

    QFileInfo fi(p_filePath);
    auto suffix = fi.suffix().toLower();
    auto it = m_suffixTypeMap.find(suffix);
    if (it != m_suffixTypeMap.end()) {
        return m_fileTypes.at(it.value());
    }

    // Treat all unknown text files as plain text files.
    if (FileUtils::isText(p_filePath)) {
        return m_fileTypes[FileType::Text];
    }

    return m_fileTypes[FileType::Others];
}

const FileType &FileTypeHelper::getFileTypeBySuffix(const QString &p_suffix) const
{
    auto it = m_suffixTypeMap.find(p_suffix.toLower());
    if (it != m_suffixTypeMap.end()) {
        return m_fileTypes.at(it.value());
    } else {
        return m_fileTypes[FileType::Others];
    }
}

#define ADD(x, y) m_suffixTypeMap.insert((x), (y))

void FileTypeHelper::setupSuffixTypeMap()
{
    for (int i = 0; i < m_fileTypes.size(); ++i) {
        for (const auto &suffix : m_fileTypes[i].m_suffixes) {
            if (m_suffixTypeMap.contains(suffix)) {
                qWarning() << "suffix conflicts detected" << suffix << m_fileTypes[i].m_type;
            }
            m_suffixTypeMap.insert(suffix, i);
        }
    }
}

const QVector<FileType> &FileTypeHelper::getAllFileTypes() const
{
    return m_fileTypes;
}

const FileType &FileTypeHelper::getFileType(int p_type) const
{
    Q_ASSERT(p_type < m_fileTypes.size());
    return m_fileTypes[p_type];
}

const FileTypeHelper &FileTypeHelper::getInst()
{
    static FileTypeHelper helper;
    return helper;
}

bool FileTypeHelper::checkFileType(const QString &p_filePath, int p_type) const
{
    return getFileType(p_filePath).m_type == p_type;
}

const FileType &FileTypeHelper::getFileTypeByName(const QString &p_typeName) const
{
    for (const auto &ft : m_fileTypes) {
        if (ft.m_typeName == p_typeName) {
            return ft;
        }
    }

    Q_ASSERT(false);
    return m_fileTypes[FileType::Others];
}
