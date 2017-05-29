#include "vorphanfile.h"
#include <QDebug>
#include <QTextEdit>
#include <QFileInfo>
#include "utils/vutils.h"

VOrphanFile::VOrphanFile(const QString &p_path, QObject *p_parent)
    : VFile(VUtils::fileNameFromPath(p_path), p_parent, FileType::Orphan, false),
      m_path(p_path)
{
    qDebug() << "VOrphanFile" << p_path << m_name;
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
    V_ASSERT(false);
    return "";
}

bool VOrphanFile::save()
{
    V_ASSERT(false);
    return false;
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
    return "[EMPTY]";
}

VNotebook *VOrphanFile::getNotebook()
{
    return NULL;
}

void VOrphanFile::setContent(const QString & /* p_content */)
{
    V_ASSERT(false);
}

bool VOrphanFile::isInternalImageFolder(const QString &p_path) const
{
    return VUtils::basePathFromPath(p_path) == VUtils::basePathFromPath(m_path);
}
