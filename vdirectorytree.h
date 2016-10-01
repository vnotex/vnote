#ifndef VDIRECTORYTREE_H
#define VDIRECTORYTREE_H

#include <QTreeWidget>

class VDirectoryTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit VDirectoryTree(QWidget *parent = 0);

signals:

public slots:
    void setTreePath(const QString& path);

private:
    // The path of the directory tree root
    QString treePath;
};

#endif // VDIRECTORYTREE_H
