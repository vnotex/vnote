#include <QtWidgets>
#include "vdirectorytree.h"
#include "dialog/vnewdirdialog.h"
#include "vconfigmanager.h"
#include "dialog/vdirinfodialog.h"
#include "vnote.h"

VDirectoryTree::VDirectoryTree(VNote *vnote, QWidget *parent)
    : QTreeWidget(parent), vnote(vnote)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    initActions();

    connect(this, SIGNAL(itemExpanded(QTreeWidgetItem*)),
            this, SLOT(updateItemSubtree(QTreeWidgetItem*)));
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

    newSiblingDirAct = new QAction(tr("New &sibling directory"), this);
    newSiblingDirAct->setStatusTip(tr("Create a new sibling directory at current level"));
    connect(newSiblingDirAct, &QAction::triggered,
            this, &VDirectoryTree::newSiblingDirectory);

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

void VDirectoryTree::setNotebook(const QString& notebookName)
{
    if (notebook == notebookName) {
        return;
    }

    notebook = notebookName;
    treePath = "";
    if (notebook.isEmpty()) {
        clear();
        return;
    }
    const QVector<VNotebook> &notebooks = vnote->getNotebooks();
    for (int i = 0; i < notebooks.size(); ++i) {
        if (notebooks[i].getName() == notebook) {
            treePath = notebooks[i].getPath();
            break;
        }
    }
    Q_ASSERT(!treePath.isEmpty());

    updateDirectoryTree();
    if (topLevelItemCount() > 0) {
       setCurrentItem(topLevelItem(0));
    }
}

bool VDirectoryTree::validatePath(const QString &path)
{
    return QDir(path).exists();
}

void VDirectoryTree::updateDirectoryTree()
{
    updateDirectoryTreeTopLevel();

    int nrTopLevelItems = topLevelItemCount();
    for (int i = 0; i < nrTopLevelItems; ++i) {
        QTreeWidgetItem *item = topLevelItem(i);
        Q_ASSERT(item);
        QJsonObject itemJson = item->data(0, Qt::UserRole).toJsonObject();
        Q_ASSERT(!itemJson.isEmpty());
        updateDirectoryTreeOne(*item, itemJson["name"].toString(), 1);
    }
}

void VDirectoryTree::fillDirectoryTreeItem(QTreeWidgetItem &item, QJsonObject itemJson)
{
    item.setText(0, itemJson["name"].toString());
    item.setData(0, Qt::UserRole, itemJson);
}

QTreeWidgetItem* VDirectoryTree::insertDirectoryTreeItem(QTreeWidgetItem *parent, QTreeWidgetItem *preceding,
                                                         const QJsonObject &newItem)
{
    QTreeWidgetItem *item;
    if (parent) {
        if (preceding) {
            item = new QTreeWidgetItem(parent, preceding);
        } else {
            item = new QTreeWidgetItem(parent);
        }
    } else {
        if (preceding) {
            item = new QTreeWidgetItem(this, preceding);
        } else {
            item = new QTreeWidgetItem(this);
        }
    }

    fillDirectoryTreeItem(*item, newItem);
    return item;
}

void VDirectoryTree::removeDirectoryTreeItem(QTreeWidgetItem *item)
{
    delete item;
}

void VDirectoryTree::updateDirectoryTreeTopLevel()
{
    const QString &path = treePath;

    clear();

    if (!validatePath(path)) {
        qDebug() << "invalid notebook path:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook path."),
                           QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Notebook path \"%1\" either does not exist or is not valid.")
                                  .arg(path));
        msgBox.exec();
        return;
    }

    QJsonObject configJson = VConfigManager::readDirectoryConfig(path);
    if (configJson.isEmpty()) {
        qDebug() << "invalid notebook configuration for path:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook configuration."),
                           QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Notebook path \"%1\" does not contain a valid configuration file.")
                                  .arg(path));
        msgBox.exec();
        return;
    }

    // Handle sub_directories section
    QJsonArray dirJson = configJson["sub_directories"].toArray();
    QTreeWidgetItem *preItem = NULL;
    for (int i = 0; i < dirJson.size(); ++i) {
        QJsonObject dirItem = dirJson[i].toObject();
        QTreeWidgetItem *treeItem = insertDirectoryTreeItem(NULL, preItem, dirItem);
        preItem = treeItem;
    }

    qDebug() << "updated" << dirJson.size() << "top-level items";
}

void VDirectoryTree::updateDirectoryTreeOne(QTreeWidgetItem &parent, const QString &relativePath,
                                            int depth)
{
    Q_ASSERT(parent.childCount() == 0);
    // Going deep enough
    if (depth <= 0) {
        return;
    }

    qDebug() << "update directory" << relativePath;

    QString path(QDir::cleanPath(treePath + QDir::separator() + relativePath));
    if (!validatePath(path)) {
        qDebug() << "invalide notebook directory:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook directory."),
                           QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Notebook directory \"%1\" either does not exist or is not a valid notebook directory.")
                                  .arg(path));
        msgBox.exec();
        return;
    }

    QJsonObject configJson = VConfigManager::readDirectoryConfig(path);
    if (configJson.isEmpty()) {
        qDebug() << "invalid notebook configuration for directory:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook directory configuration."),
                           QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Notebook directory \"%1\" does not contain a valid configuration file.")
                                  .arg(path));
        msgBox.exec();
        return;
    }

    // Handle sub_directories section
    QJsonArray dirJson = configJson["sub_directories"].toArray();
    QTreeWidgetItem *preItem = NULL;
    for (int i = 0; i < dirJson.size(); ++i) {
        QJsonObject dirItem = dirJson[i].toObject();
        QTreeWidgetItem *treeItem = insertDirectoryTreeItem(&parent, preItem, dirItem);
        preItem = treeItem;

        // Update its sub-directory recursively
        updateDirectoryTreeOne(*treeItem, QDir::cleanPath(QDir(relativePath).filePath(dirItem["name"].toString())), depth - 1);
    }
}

QString VDirectoryTree::calculateItemRelativePath(QTreeWidgetItem *item)
{
    if (!item) {
        return ".";
    }
    QJsonObject itemJson = item->data(0, Qt::UserRole).toJsonObject();
    Q_ASSERT(!itemJson.isEmpty());
    QString name = itemJson["name"].toString();
    Q_ASSERT(!name.isEmpty());
    return QDir::cleanPath(calculateItemRelativePath(item->parent()) +
                           QDir::separator() + name);
}

void VDirectoryTree::updateItemSubtree(QTreeWidgetItem *item)
{
    QJsonObject itemJson = item->data(0, Qt::UserRole).toJsonObject();
    Q_ASSERT(!itemJson.isEmpty());
    QString relativePath = calculateItemRelativePath(item);
    int nrChild = item->childCount();
    if (nrChild == 0) {
        updateDirectoryTreeOne(*item, relativePath, 2);
    } else {
        for (int i = 0; i < nrChild; ++i) {
            QTreeWidgetItem *childItem = item->child(i);
            if (childItem->childCount() > 0) {
                continue;
            }
            QJsonObject childJson = childItem->data(0, Qt::UserRole).toJsonObject();
            Q_ASSERT(!childJson.isEmpty());
            updateDirectoryTreeOne(*childItem,
                                   QDir::cleanPath(QDir(relativePath).filePath(childJson["name"].toString())), 1);
        }
    }
}

void VDirectoryTree::contextMenuRequested(QPoint pos)
{
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
            menu.addAction(newSiblingDirAct);
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

void VDirectoryTree::newSiblingDirectory()
{
    QTreeWidgetItem *parentItem = currentItem()->parent();
    Q_ASSERT(parentItem);
    QJsonObject parentItemJson = parentItem->data(0, Qt::UserRole).toJsonObject();
    QString parentItemName = parentItemJson["name"].toString();

    QString text("&Directory name:");
    QString defaultText("new_directory");
    do {
        VNewDirDialog dialog(QString("Create a new directory under %1").arg(parentItemName), text,
                             defaultText, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (isConflictNameWithChildren(parentItem, name)) {
                text = "Name already exists.\nPlease choose another name:";
                defaultText = name;
                continue;
            }
            QTreeWidgetItem *newItem = createDirectoryAndUpdateTree(parentItem, name);
            if (newItem) {
                this->setCurrentItem(newItem);
            }
        }
        break;
    } while (true);
}

void VDirectoryTree::newSubDirectory()
{
    QTreeWidgetItem *curItem = currentItem();
    if (!curItem) {
        return;
    }
    QJsonObject curItemJson = curItem->data(0, Qt::UserRole).toJsonObject();
    QString curItemName = curItemJson["name"].toString();

    QString text("&Directory name:");
    QString defaultText("new_directory");

    do {
        VNewDirDialog dialog(QString("Create a new directory under %1").arg(curItemName), text,
                             defaultText, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (isConflictNameWithChildren(curItem, name)) {
                text = "Name already exists.\nPlease choose another name:";
                defaultText = name;
                continue;
            }
            QTreeWidgetItem *newItem = createDirectoryAndUpdateTree(curItem, name);
            if (newItem) {
                this->setCurrentItem(newItem);
            }
        }
        break;
    } while (true);
}

void VDirectoryTree::newRootDirectory()
{
    QString text("&Directory name:");
    QString defaultText("new_directory");

    do {
        VNewDirDialog dialog(tr("Create a new root directory"), text,
                             defaultText, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (isConflictNameWithChildren(NULL, name)) {
                text = "Name already exists.\nPlease choose another name:";
                defaultText = name;
                continue;
            }
            QTreeWidgetItem *newItem = createDirectoryAndUpdateTree(NULL, name);
            if (newItem) {
                this->setCurrentItem(newItem);
            }
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
    QJsonObject curItemJson = curItem->data(0, Qt::UserRole).toJsonObject();
    QString curItemName = curItemJson["name"].toString();

    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Are you sure you want to delete directory \"%1\"?")
                       .arg(curItemName), QMessageBox::Ok | QMessageBox::Cancel, this);
    msgBox.setInformativeText(tr("This will delete any files under this directory."));
    msgBox.setDefaultButton(QMessageBox::Cancel);
    if (msgBox.exec() == QMessageBox::Ok) {
        deleteDirectoryAndUpdateTree(curItem);
    }
}

QTreeWidgetItem* VDirectoryTree::createDirectoryAndUpdateTree(QTreeWidgetItem *parent,
                                                              const QString &name)
{
    QString relativePath = calculateItemRelativePath(parent);
    QString path = QDir::cleanPath(QDir(treePath).filePath(relativePath));
    QDir dir(path);
    if (!dir.mkdir(name)) {
        qWarning() << "error: fail to create directory" << name << "under" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Could not create directory \"%1\" under \"%2\".")
                           .arg(name).arg(path), QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Please check if there already exists a directory named \"%1\".").arg(name));
        msgBox.exec();
        return NULL;
    }

    QJsonObject configJson;
    configJson["version"] = "1";
    configJson["sub_directories"] = QJsonArray();
    configJson["files"] = QJsonArray();

    if (!VConfigManager::writeDirectoryConfig(QDir::cleanPath(QDir(path).filePath(name)), configJson)) {
        return NULL;
    }

    // Update parent's config file to include this new directory
    configJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!configJson.isEmpty());
    QJsonObject itemJson;
    itemJson["name"] = name;
    QJsonArray subDirArray = configJson["sub_directories"].toArray();
    subDirArray.append(itemJson);
    configJson["sub_directories"] = subDirArray;
    if (!VConfigManager::writeDirectoryConfig(path, configJson)) {
        VConfigManager::deleteDirectoryConfig(QDir::cleanPath(QDir(path).filePath(name)));
        dir.rmdir(name);
        return NULL;
    }

    return insertDirectoryTreeItem(parent, NULL, itemJson);
}

void VDirectoryTree::deleteDirectoryAndUpdateTree(QTreeWidgetItem *item)
{
    if (!item) {
        return;
    }
    QJsonObject itemJson = item->data(0, Qt::UserRole).toJsonObject();
    QString itemName = itemJson["name"].toString();
    QString parentRelativePath = calculateItemRelativePath(item->parent());

    // Update parent's config file to exclude this directory
    QString path = QDir::cleanPath(QDir(treePath).filePath(parentRelativePath));
    QJsonObject configJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!configJson.isEmpty());
    QJsonArray subDirArray = configJson["sub_directories"].toArray();
    bool deleted = false;
    for (int i = 0; i < subDirArray.size(); ++i) {
        QJsonObject ele = subDirArray[i].toObject();
        if (ele["name"].toString() == itemName) {
            subDirArray.removeAt(i);
            deleted = true;
            break;
        }
    }
    if (!deleted) {
        qWarning() << "error: fail to find" << itemName << "to delete in its parent's configuration file";
        return;
    }
    configJson["sub_directories"] = subDirArray;
    if (!VConfigManager::writeDirectoryConfig(path, configJson)) {
        qWarning() << "error: fail to update parent's configuration file to delete" << itemName;
        return;
    }

    // Delete the entire directory
    QString dirName = QDir::cleanPath(QDir(path).filePath(itemName));
    QDir dir(dirName);
    if (!dir.removeRecursively()) {
        qWarning() << "error: fail to delete" << dirName << "recursively";
    } else {
        qDebug() << "delete" << dirName << "recursively";
    }

    // Update the tree
    removeDirectoryTreeItem(item);
}

bool VDirectoryTree::isConflictNameWithChildren(const QTreeWidgetItem *parent, const QString &name)
{
    if (parent) {
        int nrChild = parent->childCount();
        for (int i = 0; i < nrChild; ++i) {
            QJsonObject childItemJson = parent->child(i)->data(0, Qt::UserRole).toJsonObject();
            Q_ASSERT(!childItemJson.isEmpty());
            if (childItemJson["name"].toString() == name) {
                return true;
            }
        }
    } else {
        int nrTopLevelItems = topLevelItemCount();
        for (int i = 0; i < nrTopLevelItems; ++i) {
            QJsonObject itemJson = topLevelItem(i)->data(0, Qt::UserRole).toJsonObject();
            Q_ASSERT(!itemJson.isEmpty());
            if (itemJson["name"].toString() == name) {
                return true;
            }
        }
    }
    return false;
}

void VDirectoryTree::currentDirectoryItemChanged(QTreeWidgetItem *currentItem)
{
    if (!currentItem) {
        emit currentDirectoryChanged(QJsonObject());
        return;
    }
    QJsonObject itemJson = currentItem->data(0, Qt::UserRole).toJsonObject();
    Q_ASSERT(!itemJson.isEmpty());
    itemJson["notebook"] = notebook;
    itemJson["relative_path"] = calculateItemRelativePath(currentItem);
    emit currentDirectoryChanged(itemJson);
}

void VDirectoryTree::editDirectoryInfo()
{
    QTreeWidgetItem *curItem = currentItem();
    if (!curItem) {
        return;
    }
    QJsonObject curItemJson = curItem->data(0, Qt::UserRole).toJsonObject();
    QString curItemName = curItemJson["name"].toString();

    QString info;
    QString defaultName = curItemName;

    do {
        VDirInfoDialog dialog(tr("Directory Information"), info, defaultName, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (name == curItemName) {
                return;
            }
            if (isConflictNameWithChildren(curItem->parent(), name)) {
                info = "Name already exists.\nPlease choose another name:";
                defaultName = name;
                continue;
            }
            renameDirectory(curItem, name);
        }
        break;
    } while (true);
}

void VDirectoryTree::renameDirectory(QTreeWidgetItem *item, const QString &newName)
{
    if (!item) {
        return;
    }
    QJsonObject itemJson = item->data(0, Qt::UserRole).toJsonObject();
    QString name = itemJson["name"].toString();

    QTreeWidgetItem *parent = item->parent();
    QString parentRelativePath = calculateItemRelativePath(parent);

    QString path = QDir::cleanPath(QDir(treePath).filePath(parentRelativePath));
    QDir dir(path);

    if (!dir.rename(name, newName)) {
        qWarning() << "error: fail to rename directory" << name << "under" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Could not rename directory \"%1\" under \"%2\".")
                           .arg(name).arg(path), QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Please check if there already exists a directory named \"%1\".").arg(name));
        msgBox.exec();
        return;
    }

    // Update parent's config file
    QJsonObject configJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!configJson.isEmpty());
    QJsonArray subDirArray = configJson["sub_directories"].toArray();
    int index = 0;
    for (index = 0; index < subDirArray.size(); ++index) {
        QJsonObject tmp = subDirArray[index].toObject();
        if (tmp["name"].toString() == name) {
            tmp["name"] = newName;
            subDirArray[index] = tmp;
            break;
        }
    }
    Q_ASSERT(index != subDirArray.size());
    configJson["sub_directories"] = subDirArray;
    if (!VConfigManager::writeDirectoryConfig(path, configJson)) {
        dir.rename(newName, name);
        return;
    }

    // Update item
    itemJson["name"] = newName;
    item->setData(0, Qt::UserRole, itemJson);
    item->setText(0, newName);

    QString oldPath = QDir::cleanPath(QDir(parentRelativePath).filePath(name));
    QString newPath = QDir::cleanPath(QDir(parentRelativePath).filePath(newName));
    qDebug() << "directory renamed" << oldPath << "to" << newPath;
    emit directoryRenamed(notebook, oldPath, newPath);
}

void VDirectoryTree::handleNotebookRenamed(const QVector<VNotebook> &notebooks,
                                           const QString &oldName, const QString &newName)
{
    if (oldName == notebook) {
        notebook = newName;
        qDebug() << "directoryTree update notebook" << oldName << "to" << newName;
    }
}
