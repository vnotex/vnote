#ifndef VFILELOCATION_H
#define VFILELOCATION_H

#include <QString>

class VFileLocation
{
public:
    VFileLocation();
    VFileLocation(const QString &p_notebook, const QString &p_relativePath);
    QString m_notebook;
    QString m_relativePath;
};

#endif // VFILELOCATION_H
