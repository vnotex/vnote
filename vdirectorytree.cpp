#include <QtGui>
#include "vdirectorytree.h"

VDirectoryTree::VDirectoryTree(QWidget *parent) : QTreeWidget(parent)
{
}

void VDirectoryTree::setTreePath(const QString& path)
{
    treePath = path;
    qDebug() << "set directory tree path:" << path;
}
