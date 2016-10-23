#ifndef VDIRECTORYTREE_H
#define VDIRECTORYTREE_H

#include <QTreeWidget>
#include <QJsonObject>

class VDirectoryTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit VDirectoryTree(QWidget *parent = 0);

signals:
    void currentDirectoryChanged(QJsonObject itemJson);

public slots:
    void setTreePath(const QString& path);
    void newRootDirectory();
    void deleteDirectory();
    void editDirectoryInfo();

private slots:
    // Read config file and pdate the subtree of @item in the directory tree.
    // If @item has no child, we will call updateDirectoryTreeOne() to update it.
    // Otherwise, we will loop all its direct-children and try to populate it if
    // it has not been populated yet.
    void updateItemSubtree(QTreeWidgetItem *item);
    void contextMenuRequested(QPoint pos);
    void newSiblingDirectory();
    void newSubDirectory();
    void currentDirectoryItemChanged(QTreeWidgetItem *currentItem);

private:
    // Clean and pdate the TreeWidget according to treePath
    void updateDirectoryTree();
    // Update the top-level items of the directory tree. Will not clean the tree at first.
    void updateDirectoryTreeTopLevel();
    // Update one directory, going into @depth levels. Not cleaning the tree item at first,
    // so you must ensure @parent has no child before calling this function.
    void updateDirectoryTreeOne(QTreeWidgetItem &parent, int depth);
    // Validate if a directory is valid
    bool validatePath(const QString &path);
    // Fill the QTreeWidgetItem according to its QJsonObject.
    // @relative_path is the path related to treePath.
    void fillDirectoryTreeItem(QTreeWidgetItem &item, QJsonObject itemJson, const QString &relativePath);
    void initActions();
    QTreeWidgetItem* createDirectoryAndUpdateTree(QTreeWidgetItem *parent, const QString &name,
                                                  const QString &description);
    void deleteDirectoryAndUpdateTree(QTreeWidgetItem *item);
    // If @name conflict with the children's names of @parent.
    bool isConflictNameWithChildren(const QTreeWidgetItem *parent, const QString &name);
    QTreeWidgetItem* insertDirectoryTreeItem(QTreeWidgetItem *parent, QTreeWidgetItem *preceding,
                                             const QJsonObject &newItem);
    void removeDirectoryTreeItem(QTreeWidgetItem *item);
    void setDirectoryInfo(QTreeWidgetItem *item, const QString &newName, const QString &newDescription);

    // The path of the directory tree root
    QString treePath;

    // Actions
    QAction *newRootDirAct;
    QAction *newSiblingDirAct;
    QAction *newSubDirAct;
    QAction *deleteDirAct;
};

#endif // VDIRECTORYTREE_H
