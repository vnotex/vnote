#include "bufferprovider.h"

#include <QFileInfo>

using namespace vnotex;

bool BufferProvider::checkFileExistsOnDisk() const
{
    return QFileInfo::exists(getContentPath());
}

QDateTime BufferProvider::getLastModifiedFromFile() const
{
    return QFileInfo(getContentPath()).lastModified();
}

bool BufferProvider::checkFileChangedOutside() const
{
    QFileInfo info(getContentPath());
    if (!info.exists() || m_lastModified != info.lastModified()) {
        return true;
    }
    return false;
}
