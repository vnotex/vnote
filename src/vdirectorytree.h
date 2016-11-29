#ifndef VDIRECTORYTREE_H
#define VDIRECTORYTREE_H

#include <QTreeWidget>
#include <QJsonObject>
#include <QPointer>
#include <QVector>
#include "vdirectory.h"
#include "vnotebook.h"

class VNote;

class VDirectoryTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit VDirectoryTree(VNote *vnote, QWidget *parent = 0);

signals:
    void currentDirectoryChanged(VDirectory *p_directory);
    void directoryRenamed(const QString &notebook, const QString &oldRelativePath,
                          const QString &newRelativePath);

public slots:
    void setNotebook(VNotebook *p_notebook);
    void newRootDirectory();
    void deleteDirectory();
    void editDirectoryInfo();
    void updateDirectoryTree();

private slots:
    void updateChildren(QTreeWidgetItem *p_item);
    void contextMenuRequested(QPoint pos);
    void newSubDirectory();
    void currentDirectoryItemChanged(QTreeWidgetItem *currentItem);

private:
    void updateDirectoryTreeOne(QTreeWidgetItem *p_parent, int depth);
    void fillTreeItem(QTreeWidgetItem &p_item, const QString &p_name,
                      VDirectory *p_directory, const QIcon &p_icon);
    void initActions();
    // New directories added to @p_item. Update @p_item's children items.
    // If @p_item is NULL, then top level items added.
    QVector<QTreeWidgetItem *> updateItemChildrenAdded(QTreeWidgetItem *p_item);
    inline QPointer<VDirectory> getVDirectory(QTreeWidgetItem *p_item);

    VNote *vnote;
    QPointer<VNotebook> m_notebook;

    // Actions
    QAction *newRootDirAct;
    QAction *newSiblingDirAct;
    QAction *newSubDirAct;
    QAction *deleteDirAct;
    QAction *dirInfoAct;
};

inline QPointer<VDirectory> VDirectoryTree::getVDirectory(QTreeWidgetItem *p_item)
{
    Q_ASSERT(p_item);
    return p_item->data(0, Qt::UserRole).value<VDirectory *>();
}

#endif // VDIRECTORYTREE_H
