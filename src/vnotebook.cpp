#include "vnotebook.h"

VNotebook::VNotebook()
{

}

VNotebook::VNotebook(const QString &name, const QString &path)
    : name(name), path(path)
{
}

QString VNotebook::getName() const
{
    return this->name;
}

QString VNotebook::getPath() const
{
    return this->path;
}

void VNotebook::setName(const QString &name)
{
    this->name = name;
}

void VNotebook::setPath(const QString &path)
{
    this->path = path;
}
