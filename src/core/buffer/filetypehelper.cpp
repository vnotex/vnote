#include "filetypehelper.h"

#include <QFileInfo>

#include <utils/fileutils.h>

using namespace vnotex;

const FileType FileTypeHelper::s_markdownFileType = "markdown";

const FileType FileTypeHelper::s_textFileType = "text";

const FileType FileTypeHelper::s_unknownFileType = "unknown";

QSharedPointer<QMap<QString, FileType>> FileTypeHelper::s_fileTypeMap;

FileType FileTypeHelper::fileType(const QString &p_filePath)
{
    Q_ASSERT(!p_filePath.isEmpty());

    if (!s_fileTypeMap) {
        init();
    }

    QFileInfo fi(p_filePath);
    auto suffix = fi.suffix().toLower();
    auto it = s_fileTypeMap->find(suffix);
    if (it != s_fileTypeMap->end()) {
        return it.value();
    }

    // Treat all unknown text files as plain text files.
    if (FileUtils::isText(p_filePath)) {
        return s_fileTypeMap->value(QStringLiteral("txt"));
    }

    return s_unknownFileType;
}

#define ADD(x, y) s_fileTypeMap->insert((x), (y))

void FileTypeHelper::init()
{
    // TODO: load mapping from configuration file.
    s_fileTypeMap.reset(new QMap<QString, FileType>());

    ADD(QStringLiteral("md"), s_markdownFileType);
    ADD(QStringLiteral("markdown"), s_markdownFileType);
    ADD(QStringLiteral("mkd"), s_markdownFileType);

    ADD(QStringLiteral("txt"), s_textFileType);
    ADD(QStringLiteral("text"), s_textFileType);
    ADD(QStringLiteral("log"), s_textFileType);
}
