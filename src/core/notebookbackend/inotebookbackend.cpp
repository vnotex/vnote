#include "inotebookbackend.h"

#include <QDir>

#include <exception.h>
#include <utils/pathutils.h>

using namespace vnotex;

void INotebookBackend::constrainPath(const QString &p_path) const
{
    if (!PathUtils::pathContains(m_rootPath, p_path)) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("path (%1) does not locate in root folder (%2)")
                                   .arg(p_path, m_rootPath));
    }
}

QString INotebookBackend::getFullPath(const QString &p_path) const
{
    constrainPath(p_path);
    return QDir(m_rootPath).filePath(p_path);
}
