#include "vnotebook.h"

VNotebook::VNotebook(QObject *parent)
    : QObject(parent)
{

}

VNotebook::VNotebook(const QString &name, const QString &path, QObject *parent)
    : QObject(parent), m_name(name), m_path(path)
{
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

void VNotebook::close(bool p_forced)
{
    //TODO
}
