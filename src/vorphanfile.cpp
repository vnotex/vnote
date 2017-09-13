#include "vorphanfile.h"
#include <QDebug>
#include <QTextEdit>
#include <QFileInfo>
#include <QDir>
#include "utils/vutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

VOrphanFile::VOrphanFile(const QString &p_path, QObject *p_parent,
                         bool p_modifiable, bool p_systemFile)
    : VFile(p_parent,
            VUtils::fileNameFromPath(p_path),
            FileType::Orphan,
            p_modifiable,
            QDateTime(),
            QDateTime()),
      m_path(p_path), m_notebookName(tr("[EXTERNAL]")), m_systemFile(p_systemFile)
{
    qDebug() << "VOrphanFile" << p_path << m_name << p_modifiable;
}

bool VOrphanFile::open()
{
    Q_ASSERT(!m_name.isEmpty());
    if (m_opened) {
        return true;
    }

    Q_ASSERT(m_content.isEmpty());
    Q_ASSERT(QFileInfo::exists(m_path));

    m_content = VUtils::readFileFromDisk(m_path);
    m_modified = false;
    m_opened = true;
    return true;
}

QString VOrphanFile::fetchPath() const
{
    return m_path;
}

QString VOrphanFile::fetchRelativePath() const
{
    return m_path;
}

QString VOrphanFile::fetchBasePath() const
{
    return VUtils::basePathFromPath(m_path);
}

QString VOrphanFile::fetchImagePath() const
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

bool VOrphanFile::save()
{
    Q_ASSERT(m_opened);
    Q_ASSERT(m_modifiable);
    return VUtils::writeFileToDisk(fetchPath(), m_content);
}

void VOrphanFile::setName(const QString & /* p_name */)
{
    V_ASSERT(false);
}

VDirectory *VOrphanFile::getDirectory()
{
    return NULL;
}

const VDirectory *VOrphanFile::getDirectory() const
{
    return NULL;
}

QString VOrphanFile::getNotebookName() const
{
    return m_notebookName;
}

void VOrphanFile::setNotebookName(const QString &p_notebook)
{
    m_notebookName = p_notebook;
}

VNotebook *VOrphanFile::getNotebook()
{
    return NULL;
}

void VOrphanFile::setContent(const QString & p_content)
{
    m_content = p_content;
}

bool VOrphanFile::isInternalImageFolder(const QString &p_path) const
{
    return VUtils::equalPath(VUtils::basePathFromPath(p_path),
                             fetchBasePath())
           || VUtils::equalPath(p_path, fetchImagePath());
}

bool VOrphanFile::rename(const QString &p_name)
{
    QDir dir(fetchBasePath());
    if (!dir.rename(m_name, p_name)) {
        qWarning() << "fail to rename note" << m_name << "to" << p_name << "in disk";
        return false;
    }

    m_name = p_name;
    m_path = dir.filePath(m_name);
    return true;
}

void VOrphanFile::setImageFolder(const QString &p_path)
{
    qDebug() << "orphan file" << fetchPath() << "image folder"
             << m_imageFolder << "->" << p_path;
    m_imageFolder = p_path;
}

bool VOrphanFile::isRelativeImageFolder() const
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
