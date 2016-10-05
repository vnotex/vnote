#ifndef VDIRECTORYTREE_H
#define VDIRECTORYTREE_H

#include <QTreeWidget>

class QJsonObject;

class VDirectoryTree : public QTreeWidget
{
    Q_OBJECT
public:
    VDirectoryTree(const QString &dirConfigFileName, QWidget *parent = 0);

signals:

public slots:
    void setTreePath(const QString& path);

private slots:
    // Read config file and pdate the subtree of @item in the directory tree.
    // If @item has no child, we will call updateDirectoryTreeOne() to update it.
    // Otherwise, we will loop all its direct-children and try to populate it if
    // it has not been populated yet.
    void updateItemSubtree(QTreeWidgetItem *item);
    void contextMenuRequested(QPoint pos);
    void newSiblingDirectory();
    void newSubDirectory();
    void newRootDirectory();

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
    // Validate if a directory config file is valid
    bool validateDirConfigFile(const QJsonObject &configJson);
    // Fill the QTreeWidgetItem according to its QJsonObject.
    // @relative_path is the path related to treePath.
    void fillDirectoryTreeItem(QTreeWidgetItem &item, QJsonObject itemJson, const QString &relativePath);
    void initialActions();
    QTreeWidgetItem* createDirectoryAndUpdateTree(QTreeWidgetItem *parent, const QString &name,
                                                  const QString &description);
    // If @name conflict with the children's names of @parent.
    bool isConflictNameWithChildren(const QTreeWidgetItem *parent, const QString &name);
    // Read config from the directory config json file into a QJsonObject
    QJsonObject readDirectoryConfig(const QString &path);
    bool writeDirectoryConfig(const QString &path, const QJsonObject &configJson);
    bool deleteDirectoryConfig(const QString &path);
    QTreeWidgetItem* insertDirectoryTreeItem(QTreeWidgetItem *parent, QTreeWidgetItem *preceding,
                                             const QJsonObject &newItem);

    // The path of the directory tree root
    QString treePath;
    // The name of the config file in each subdirectory
    QString dirConfigFileName;

    // Actions
    QAction *newRootDirAct;
    QAction *newSiblingDirAct;
    QAction *newSubDirAct;
};

#endif // VDIRECTORYTREE_H
