#ifndef VNOTEBOOK_H
#define VNOTEBOOK_H

#include <QString>

class VNotebook
{
public:
    VNotebook();
    VNotebook(const QString &name, const QString &path);

    QString getName() const;
    QString getPath() const;
    void setName(const QString &name);
    void setPath(const QString &path);

private:
    QString name;
    QString path;
};

#endif // VNOTEBOOK_H
