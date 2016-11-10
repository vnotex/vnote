#ifndef VDIRECTORYTREE_H
#define VDIRECTORYTREE_H

#include <QTreeWidget>
#include <QJsonObject>
#include "vnotebook.h"

class VNote;

class VDirectoryTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit VDirectoryTree(VNote *vnote, QWidget *parent = 0);

signals:
    void currentDirectoryChanged(QJsonObject itemJson);
    void directoryRenamed(const QString &notebook, const QString &oldRelativePath,
                          const QString &newRelativePath);

public slots:
    void setNotebook(const QString &notebookName);
    void newRootDirectory();
    void deleteDirectory();
    void editDirectoryInfo();
    void handleNotebookRenamed(const QVector<VNotebook> &notebooks, const QString &oldName,
                               const QString &newName);

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
    QString calculateItemRelativePath(QTreeWidgetItem *item);
    // Clean and pdate the TreeWidget according to treePath
    void updateDirectoryTree();
    // Update the top-level items of the directory tree. Will not clean the tree at first.
    void updateDirectoryTreeTopLevel();

    // @relativePath is the relative path of the direcotry we are updating
    void updateDirectoryTreeOne(QTreeWidgetItem &parent, const QString &relativePath, int depth);
    // Validate if a directory is valid
    bool validatePath(const QString &path);

    void fillDirectoryTreeItem(QTreeWidgetItem &item, QJsonObject itemJson);
    void initActions();
    QTreeWidgetItem* createDirectoryAndUpdateTree(QTreeWidgetItem *parent, const QString &name);
    void deleteDirectoryAndUpdateTree(QTreeWidgetItem *item);
    // If @name conflict with the children's names of @parent.
    bool isConflictNameWithChildren(const QTreeWidgetItem *parent, const QString &name);

    QTreeWidgetItem* insertDirectoryTreeItem(QTreeWidgetItem *parent, QTreeWidgetItem *preceding,
                                             const QJsonObject &newItem);
    void removeDirectoryTreeItem(QTreeWidgetItem *item);
    void renameDirectory(QTreeWidgetItem *item, const QString &newName);

    VNote *vnote;

    QString notebook;
    // Used for cache
    QString treePath;

    // Actions
    QAction *newRootDirAct;
    QAction *newSiblingDirAct;
    QAction *newSubDirAct;
    QAction *deleteDirAct;
    QAction *dirInfoAct;
};

#endif // VDIRECTORYTREE_H
