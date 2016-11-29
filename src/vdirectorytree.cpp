#include <QtWidgets>
#include "vdirectorytree.h"
#include "dialog/vnewdirdialog.h"
#include "vconfigmanager.h"
#include "dialog/vdirinfodialog.h"
#include "vnote.h"
#include "vdirectory.h"
#include "utils/vutils.h"

VDirectoryTree::VDirectoryTree(VNote *vnote, QWidget *parent)
    : QTreeWidget(parent), vnote(vnote)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    initActions();

    connect(this, SIGNAL(itemExpanded(QTreeWidgetItem*)),
            this, SLOT(updateChildren(QTreeWidgetItem*)));
    connect(this, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(contextMenuRequested(QPoint)));
    connect(this, &VDirectoryTree::currentItemChanged,
            this, &VDirectoryTree::currentDirectoryItemChanged);
}

void VDirectoryTree::initActions()
{
    newRootDirAct = new QAction(QIcon(":/resources/icons/create_rootdir.svg"),
                                tr("New &root directory"), this);
    newRootDirAct->setStatusTip(tr("Create a new root directory in current notebook"));
    connect(newRootDirAct, &QAction::triggered,
            this, &VDirectoryTree::newRootDirectory);

    newSubDirAct = new QAction(tr("&New sub-directory"), this);
    newSubDirAct->setStatusTip(tr("Create a new sub-directory"));
    connect(newSubDirAct, &QAction::triggered,
            this, &VDirectoryTree::newSubDirectory);

    deleteDirAct = new QAction(QIcon(":/resources/icons/delete_dir.svg"),
                               tr("&Delete"), this);
    deleteDirAct->setStatusTip(tr("Delete selected directory"));
    connect(deleteDirAct, &QAction::triggered,
            this, &VDirectoryTree::deleteDirectory);

    dirInfoAct = new QAction(QIcon(":/resources/icons/dir_info.svg"),
                             tr("&Info"), this);
    dirInfoAct->setStatusTip(tr("View and edit current directory's information"));
    connect(dirInfoAct, &QAction::triggered,
            this, &VDirectoryTree::editDirectoryInfo);
}

void VDirectoryTree::setNotebook(VNotebook *p_notebook)
{
    if (m_notebook == p_notebook) {
        return;
    }

    if (m_notebook) {
        // Disconnect
        disconnect((VNotebook *)m_notebook, &VNotebook::contentChanged,
                   this, &VDirectoryTree::updateDirectoryTree);
    }
    m_notebook = p_notebook;
    if (m_notebook) {
        connect((VNotebook *)m_notebook, &VNotebook::contentChanged,
                this, &VDirectoryTree::updateDirectoryTree);
    } else {
        clear();
        return;
    }
    if (!m_notebook->open()) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            QString("Failed to open notebook %1").arg(m_notebook->getName()), "",
                            QMessageBox::Ok, QMessageBox::Ok, this);
        clear();
        return;
    }
    updateDirectoryTree();
}

void VDirectoryTree::fillTreeItem(QTreeWidgetItem &p_item, const QString &p_name,
                                  VDirectory *p_directory, const QIcon &p_icon)
{
    p_item.setText(0, p_name);
    p_item.setData(0, Qt::UserRole, QVariant::fromValue(p_directory));
    p_item.setIcon(0, p_icon);
}

void VDirectoryTree::updateDirectoryTree()
{
    clear();
    VDirectory *rootDir = m_notebook->getRootDir();
    const QVector<VDirectory *> &subDirs = rootDir->getSubDirs();
    for (int i = 0; i < subDirs.size(); ++i) {
        VDirectory *dir = subDirs[i];
        QTreeWidgetItem *item = new QTreeWidgetItem(this);

        fillTreeItem(*item, dir->getName(), dir,
                     QIcon(":/resources/icons/dir_item.svg"));

        updateDirectoryTreeOne(item, 1);
    }
    setCurrentItem(topLevelItem(0));
}

void VDirectoryTree::updateDirectoryTreeOne(QTreeWidgetItem *p_parent, int depth)
{
    Q_ASSERT(p_parent->childCount() == 0);
    if (depth <= 0) {
        return;
    }
    VDirectory *dir = getVDirectory(p_parent);
    if (!dir->open()) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            QString("Failed to open directory %1").arg(dir->getName()), "",
                            QMessageBox::Ok, QMessageBox::Ok, this);
        return;
    }
    const QVector<VDirectory *> &subDirs = dir->getSubDirs();
    for (int i = 0; i < subDirs.size(); ++i) {
        VDirectory *subDir = subDirs[i];
        QTreeWidgetItem *item = new QTreeWidgetItem(p_parent);

        fillTreeItem(*item, subDir->getName(), subDir,
                     QIcon(":/resources/icons/dir_item.svg"));

        updateDirectoryTreeOne(item, depth - 1);
    }
}

// Update @p_item's children items
void VDirectoryTree::updateChildren(QTreeWidgetItem *p_item)
{
    Q_ASSERT(p_item);
    int nrChild = p_item->childCount();
    if (nrChild == 0) {
        return;
    }
    for (int i = 0; i < nrChild; ++i) {
        QTreeWidgetItem *childItem = p_item->child(i);
        if (childItem->childCount() > 0) {
            continue;
        }
        updateDirectoryTreeOne(childItem, 1);
    }
}

QVector<QTreeWidgetItem *> VDirectoryTree::updateItemChildrenAdded(QTreeWidgetItem *p_item)
{
    QVector<QTreeWidgetItem *> ret;
    QPointer<VDirectory> parentDir;
    if (p_item) {
        parentDir = getVDirectory(p_item);
    } else {
        parentDir = m_notebook->getRootDir();
    }
    const QVector<VDirectory *> &subDirs = parentDir->getSubDirs();
    for (int i = 0; i < subDirs.size(); ++i) {
        int nrChild;
        if (p_item) {
            nrChild = p_item->childCount();
        } else {
            nrChild = this->topLevelItemCount();
        }
        VDirectory *dir = subDirs[i];
        QTreeWidgetItem *item;
        if (i >= nrChild) {
            if (p_item) {
                item = new QTreeWidgetItem(p_item);
            } else {
                item = new QTreeWidgetItem(this);
            }
            fillTreeItem(*item, dir->getName(), dir, QIcon(":/resources/icons/dir_item.svg"));
            updateDirectoryTreeOne(item, 1);
            ret.append(item);
        } else {
            VDirectory *itemDir;
            if (p_item) {
                itemDir = getVDirectory(p_item->child(i));
            } else {
                itemDir = getVDirectory(topLevelItem(i));
            }
            if (itemDir != dir) {
                item = new QTreeWidgetItem();
                if (p_item) {
                    p_item->insertChild(i, item);
                } else {
                    this->insertTopLevelItem(i, item);
                }
                fillTreeItem(*item, dir->getName(), dir, QIcon(":/resources/icons/dir_item.svg"));
                updateDirectoryTreeOne(item, 1);
                ret.append(item);
            }
        }
    }
    qDebug() << ret.size() << "items added";
    return ret;
}

void VDirectoryTree::contextMenuRequested(QPoint pos)
{
    if (!m_notebook) {
        return;
    }
    QTreeWidgetItem *item = itemAt(pos);
    QMenu menu(this);

    if (!item) {
        // Context menu on the free space of the QTreeWidget
        menu.addAction(newRootDirAct);
    } else {
        // Context menu on a QTreeWidgetItem
        if (item->parent()) {
            // Low-level item
            menu.addAction(newSubDirAct);
        } else {
            // Top-level item
            menu.addAction(newRootDirAct);
            menu.addAction(newSubDirAct);
        }
        menu.addAction(deleteDirAct);
        menu.addAction(dirInfoAct);
    }
    menu.exec(mapToGlobal(pos));
}

void VDirectoryTree::newSubDirectory()
{
    if (!m_notebook) {
        return;
    }
    QTreeWidgetItem *curItem = currentItem();
    if (!curItem) {
        return;
    }
    VDirectory *curDir = getVDirectory(curItem);

    QString info = QString("Create sub-directory under %1.").arg(curDir->getName());
    QString text("&Directory name:");
    QString defaultText("new_directory");

    do {
        VNewDirDialog dialog(tr("Create directory"), info, text, defaultText, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (curDir->findSubDirectory(name)) {
                info = QString("Name already exists under %1.\nPlease choose another name.").arg(curDir->getName());
                defaultText = name;
                continue;
            }
            VDirectory *subDir = curDir->createSubDirectory(name);
            if (!subDir) {
                VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                    QString("Failed to create directory %1.").arg(name), "",
                                    QMessageBox::Ok, QMessageBox::Ok, this);
                return;
            }
            QVector<QTreeWidgetItem *> items = updateItemChildrenAdded(curItem);
            Q_ASSERT(items.size() == 1);
            setCurrentItem(items[0]);
        }
        break;
    } while (true);
}

void VDirectoryTree::newRootDirectory()
{
    if (!m_notebook) {
        return;
    }
    QString info = QString("Create root directory in notebook %1.").arg(m_notebook->getName());
    QString text("&Directory name:");
    QString defaultText("new_directory");
    VDirectory *rootDir = m_notebook->getRootDir();
    do {
        VNewDirDialog dialog(tr("Create root directory"), info, text, defaultText, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (rootDir->findSubDirectory(name)) {
                info = QString("Name already exists in notebook %1.\nPlease choose another name.").arg(m_notebook->getName());
                defaultText = name;
                continue;
            }
            VDirectory *subDir = rootDir->createSubDirectory(name);
            if (!subDir) {
                VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                    QString("Failed to create directory %1.").arg(name), "",
                                    QMessageBox::Ok, QMessageBox::Ok, this);
                return;
            }
            QVector<QTreeWidgetItem *> items = updateItemChildrenAdded(NULL);
            Q_ASSERT(items.size() == 1);
            setCurrentItem(items[0]);
        }
        break;
    } while (true);
}

void VDirectoryTree::deleteDirectory()
{
    QTreeWidgetItem *curItem = currentItem();
    if (!curItem) {
        return;
    }
    VDirectory *curDir = getVDirectory(curItem);
    int ret = VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                  QString("Are you sure to delete directory %1?").arg(curDir->getName()),
                                  tr("This will delete any files under this directory."), QMessageBox::Ok | QMessageBox::Cancel,
                                  QMessageBox::Ok, this);
    if (ret == QMessageBox::Ok) {
        VDirectory *parentDir = curDir->getParentDirectory();
        Q_ASSERT(parentDir);
        parentDir->deleteSubDirectory(curDir);
        delete curItem;
    }
}

void VDirectoryTree::currentDirectoryItemChanged(QTreeWidgetItem *currentItem)
{
    if (!currentItem) {
        emit currentDirectoryChanged(NULL);
        return;
    }
    emit currentDirectoryChanged(getVDirectory(currentItem));
}

void VDirectoryTree::editDirectoryInfo()
{
    QTreeWidgetItem *curItem = currentItem();
    if (!curItem) {
        return;
    }

    VDirectory *curDir = getVDirectory(curItem);
    VDirectory *parentDir = curDir->getParentDirectory();
    QString curName = curDir->getName();
    QString info;
    QString defaultName = curName;

    do {
        VDirInfoDialog dialog(tr("Directory Information"), info, defaultName, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (name == curName) {
                return;
            }
            if (parentDir->findSubDirectory(name)) {
                info = "Name already exists.\nPlease choose another name.";
                defaultName = name;
                continue;
            }
            if (!curDir->rename(name)) {
                VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                    QString("Failed to rename directory %1.").arg(curName), "",
                                    QMessageBox::Ok, QMessageBox::Ok, this);
                return;
            }
            curItem->setText(0, name);
        }
        break;
    } while (true);
}
