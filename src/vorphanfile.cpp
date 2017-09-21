#include "vorphanfile.h"
#include <QDebug>
#include <QTextEdit>
#include <QFileInfo>
#include <QDir>
#include "utils/vutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

VOrphanFile::VOrphanFile(QObject *p_parent,
                         const QString &p_path,
                         bool p_modifiable,
                         bool p_systemFile)
    : VFile(p_parent,
            VUtils::fileNameFromPath(p_path),
            FileType::Orphan,
            p_modifiable,
            QDateTime(),
            QDateTime()),
      m_path(p_path),
      m_systemFile(p_systemFile)
{
}

QString VOrphanFile::fetchPath() const
{
    return m_path;
}

QString VOrphanFile::fetchBasePath() const
{
    return VUtils::basePathFromPath(m_path);
}

QString VOrphanFile::fetchImageFolderPath() const
{
    QString folder = m_imageFolder;
    if (m_imageFolder.isEmpty()) {
        folder = g_config->getImageFolderExt();
    }

    QFileInfo fi(folder);
    if (fi.isAbsolute()) {
        return folder;
    } else {
        return QDir(fetchBasePath()).filePath(folder);
    }
}

bool VOrphanFile::useRelativeImageFolder() const
{
    QString folder = m_imageFolder;
    if (m_imageFolder.isEmpty()) {
        folder = g_config->getImageFolderExt();
    }

    return !QFileInfo(folder).isAbsolute();
}

QString VOrphanFile::getImageFolderInLink() const
{
    QString folder = m_imageFolder;
    if (m_imageFolder.isEmpty()) {
        folder = g_config->getImageFolderExt();
    }

    return folder;
}
