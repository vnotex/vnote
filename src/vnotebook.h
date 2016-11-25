#ifndef VNOTEBOOK_H
#define VNOTEBOOK_H

#include <QObject>
#include <QString>

class VNotebook : public QObject
{
    Q_OBJECT
public:
    VNotebook(QObject *parent = 0);
    VNotebook(const QString &name, const QString &path, QObject *parent = 0);

    // Close all the directory and files of this notebook.
    // If @p_forced, unsaved files will also be closed without a confirm.
    void close(bool p_forced);

    QString getName() const;
    QString getPath() const;
    void setName(const QString &name);
    void setPath(const QString &path);

private:
    QString m_name;
    QString m_path;
};

#endif // VNOTEBOOK_H
