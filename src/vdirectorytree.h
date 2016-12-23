#ifndef VDIRECTORYTREE_H
#define VDIRECTORYTREE_H

#include <QTreeWidget>
#include <QJsonObject>
#include <QPointer>
#include <QVector>
#include "vdirectory.h"
#include "vnotebook.h"

class VNote;
class VEditArea;

class VDirectoryTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit VDirectoryTree(VNote *vnote, QWidget *parent = 0);
    inline void setEditArea(VEditArea *p_editArea);

signals:
    void currentDirectoryChanged(VDirectory *p_directory);
    void directoryUpdated(const VDirectory *p_directory);

public slots:
    void setNotebook(VNotebook *p_notebook);
    void newRootDirectory();
    void deleteDirectory();
    void editDirectoryInfo();
    void updateDirectoryTree();

private slots:
    void handleItemExpanded(QTreeWidgetItem *p_item);
    void handleItemCollapsed(QTreeWidgetItem *p_item);
    void contextMenuRequested(QPoint pos);
    void newSubDirectory();
    void currentDirectoryItemChanged(QTreeWidgetItem *currentItem);
    void copySelectedDirectories(bool p_cut = false);
    void cutSelectedDirectories();
    void pasteDirectoriesInCurDir();

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

private:
    void updateDirectoryTreeOne(QTreeWidgetItem *p_parent, int depth);
    void fillTreeItem(QTreeWidgetItem &p_item, const QString &p_name,
                      VDirectory *p_directory, const QIcon &p_icon);
    void initActions();
    // Update @p_item's direct children only: deleted, added, renamed.
    void updateItemChildren(QTreeWidgetItem *p_item);
    // Find the corresponding item of @p_dir;
    // Return's NULL if no item is found and it is the root directory if @p_widget is true.
    QTreeWidgetItem *findVDirectory(const VDirectory *p_dir, bool &p_widget);
    inline QPointer<VDirectory> getVDirectory(QTreeWidgetItem *p_item);
    void copyDirectoryInfoToClipboard(const QJsonArray &p_dirs, bool p_cut);
    void pasteDirectories(VDirectory *p_destDir);
    bool copyDirectory(VDirectory *p_destDir, const QString &p_destName,
                       VDirectory *p_srcDir, bool p_cut);
    void updateChildren(QTreeWidgetItem *p_item);

    VNote *vnote;
    QPointer<VNotebook> m_notebook;
    QVector<QPointer<VDirectory> > m_copiedDirs;
    VEditArea *m_editArea;

    // Actions
    QAction *newRootDirAct;
    QAction *newSiblingDirAct;
    QAction *newSubDirAct;
    QAction *deleteDirAct;
    QAction *dirInfoAct;
    QAction *copyAct;
    QAction *cutAct;
    QAction *pasteAct;
};

inline QPointer<VDirectory> VDirectoryTree::getVDirectory(QTreeWidgetItem *p_item)
{
    Q_ASSERT(p_item);
    return p_item->data(0, Qt::UserRole).value<VDirectory *>();
}

inline void VDirectoryTree::setEditArea(VEditArea *p_editArea)
{
    m_editArea = p_editArea;
}

#endif // VDIRECTORYTREE_H
