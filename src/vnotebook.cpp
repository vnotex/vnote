#include "vnotebook.h"
#include "vdirectory.h"
#include "utils/vutils.h"

VNotebook::VNotebook(const QString &name, const QString &path, QObject *parent)
    : QObject(parent), m_name(name), m_path(path)
{
    m_rootDir = new VDirectory(this, VUtils::directoryNameFromPath(path));
}

VNotebook::~VNotebook()
{
    delete m_rootDir;
}

QString VNotebook::getName() const
{
    return m_name;
}

QString VNotebook::getPath() const
{
    return m_path;
}

void VNotebook::setName(const QString &name)
{
    m_name = name;
}

void VNotebook::setPath(const QString &path)
{
    m_path = path;
}

void VNotebook::close()
{
    m_rootDir->close();
}

bool VNotebook::open()
{
    return m_rootDir->open();
}
