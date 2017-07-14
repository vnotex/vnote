#include "vorphanfile.h"
#include <QDebug>
#include <QTextEdit>
#include <QFileInfo>
#include <QDir>
#include "utils/vutils.h"
#include "vconfigmanager.h"

extern VConfigManager vconfig;

VOrphanFile::VOrphanFile(const QString &p_path, QObject *p_parent, bool p_modifiable)
    : VFile(VUtils::fileNameFromPath(p_path), p_parent, FileType::Orphan, p_modifiable),
      m_path(p_path), m_notebookName("[EXTERNAL]")
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

QString VOrphanFile::retrivePath() const
{
    return m_path;
}

QString VOrphanFile::retriveRelativePath() const
{
    return m_path;
}

QString VOrphanFile::retriveBasePath() const
{
    return VUtils::basePathFromPath(m_path);
}

QString VOrphanFile::retriveImagePath() const
{
    QString folder = m_imageFolder;
    if (m_imageFolder.isEmpty()) {
        folder = vconfig.getImageFolderExt();
    }

    QFileInfo fi(folder);
    if (fi.isAbsolute()) {
        return folder;
    } else {
        return QDir(retriveBasePath()).filePath(folder);
    }
}

bool VOrphanFile::save()
{
    Q_ASSERT(m_opened);
    Q_ASSERT(m_modifiable);
    return VUtils::writeFileToDisk(retrivePath(), m_content);
}

void VOrphanFile::convert(DocType /* p_curType */, DocType /* p_targetType */)
{
    V_ASSERT(false);
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
                             retriveBasePath())
           || VUtils::equalPath(p_path, retriveImagePath());
}

bool VOrphanFile::rename(const QString &p_name)
{
    QDir dir(retriveBasePath());
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
    m_imageFolder = p_path;
}

bool VOrphanFile::isRelativeImageFolder() const
{
    QString folder = m_imageFolder;
    if (m_imageFolder.isEmpty()) {
        folder = vconfig.getImageFolderExt();
    }

    return !QFileInfo(folder).isAbsolute();
}

QString VOrphanFile::getImageFolderInLink() const
{
    QString folder = m_imageFolder;
    if (m_imageFolder.isEmpty()) {
        folder = vconfig.getImageFolderExt();
    }

    return folder;
}
