#include <QtWidgets>
#include <QJsonObject>
#include "vdirectorytree.h"
#include "vnewdirdialog.h"

VDirectoryTree::VDirectoryTree(const QString &dirConfigFileName, QWidget *parent)
    : QTreeWidget(parent), dirConfigFileName(dirConfigFileName)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    initialActions();

    connect(this, SIGNAL(itemExpanded(QTreeWidgetItem*)),
            this, SLOT(updateItemSubtree(QTreeWidgetItem*)));
    connect(this, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(contextMenuRequested(QPoint)));
}

void VDirectoryTree::initialActions()
{
    newRootDirAct = new QAction(tr("New &root directory"), this);
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

    deleteDirAct = new QAction(tr("&Delete"), this);
    deleteDirAct->setStatusTip(tr("Delete selected directory"));
    connect(deleteDirAct, &QAction::triggered,
            this, &VDirectoryTree::deleteDirectory);
}

void VDirectoryTree::setTreePath(const QString& path)
{
    if (path == treePath) {
        return;
    }

    treePath = path;
    qDebug() << "set directory tree path:" << path;

    updateDirectoryTree();
}

bool VDirectoryTree::validatePath(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) {
        return false;
    }

    QString configFile = dir.filePath(dirConfigFileName);
    QFileInfo fileInfo(configFile);
    return fileInfo.exists() && fileInfo.isFile();
}

void VDirectoryTree::updateDirectoryTree()
{
    updateDirectoryTreeTopLevel();

    int nrTopLevelItems = topLevelItemCount();
    for (int i = 0; i < nrTopLevelItems; ++i) {
        QTreeWidgetItem *item = topLevelItem(i);
        Q_ASSERT(item);
        updateDirectoryTreeOne(*item, 1);
    }
}

// QJsonObject stored in each item's data[UserRole]:
// 1. @item's related item in its parent's [sub_directories] section;
// 2. "relative_path": the path where this item exists, relative to the treePath.
void VDirectoryTree::fillDirectoryTreeItem(QTreeWidgetItem &item, QJsonObject itemJson, const QString &relativePath)
{
    item.setText(0, itemJson["name"].toString());
    QString description = itemJson["description"].toString();
    if (!description.isEmpty()) {
        item.setToolTip(0, description);
    }
    itemJson["relative_path"] = relativePath;
    item.setData(0, Qt::UserRole, itemJson);
}

QTreeWidgetItem* VDirectoryTree::insertDirectoryTreeItem(QTreeWidgetItem *parent, QTreeWidgetItem *preceding,
                                                         const QJsonObject &newItem)
{
    QTreeWidgetItem *item;
    QString relativePath;
    if (parent) {
        if (preceding) {
            item = new QTreeWidgetItem(parent, preceding);
        } else {
            item = new QTreeWidgetItem(parent);
        }
        QJsonObject parentJson = parent->data(0, Qt::UserRole).toJsonObject();
        Q_ASSERT(!parentJson.isEmpty());
        QString parentRelativePath = parentJson["relative_path"].toString();
        QString parentName = parentJson["name"].toString();
        relativePath = QDir(parentRelativePath).filePath(parentName);
    } else {
        if (preceding) {
            item = new QTreeWidgetItem(this, preceding);
        } else {
            item = new QTreeWidgetItem(this);
        }
        relativePath = "";
    }

    fillDirectoryTreeItem(*item, newItem, relativePath);
    qDebug() << "insert new Item name:" << newItem["name"].toString()
             << "relative_path:" << relativePath;
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
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook path."));
        msgBox.setInformativeText(QString("Notebook path \"%1\" either does not exist or is not valid.")
                                  .arg(path));
        msgBox.exec();
        return;
    }

    QJsonObject configJson = readDirectoryConfig(path);
    if (!validateDirConfigFile(configJson)) {
        qDebug() << "invalid notebook configuration for path:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook configuration."));
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

void VDirectoryTree::updateDirectoryTreeOne(QTreeWidgetItem &parent, int depth)
{
    Q_ASSERT(parent.childCount() == 0);
    // Going deep enough
    if (depth <= 0) {
        return;
    }
    QJsonObject parentJson = parent.data(0, Qt::UserRole).toJsonObject();
    QString relativePath = QDir(parentJson["relative_path"].toString()).filePath(parentJson["name"].toString());
    QString path(QDir::cleanPath(treePath + QDir::separator() + relativePath));
    if (!validatePath(path)) {
        qDebug() << "invalide notebook directory:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook directory."));
        msgBox.setInformativeText(QString("Notebook directory \"%1\" either does not exist or is not a valid notebook directory.")
                                  .arg(path));
        msgBox.exec();
        return;
    }

    QJsonObject configJson = readDirectoryConfig(path);
    if (!validateDirConfigFile(configJson)) {
        qDebug() << "invalid notebook configuration for directory:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook directory configuration."));
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
        QTreeWidgetItem *treeItem = insertDirectoryTreeItem(&parent, preItem, dirItem);
        preItem = treeItem;

        // Update its sub-directory recursively
        updateDirectoryTreeOne(*treeItem, depth - 1);
    }
}

QJsonObject VDirectoryTree::readDirectoryConfig(const QString &path)
{
    QString configFile = QDir(path).filePath(dirConfigFileName);

    qDebug() << "read config file:" << configFile;
    QFile config(configFile);
    if (!config.open(QIODevice::ReadOnly)) {
        qWarning() << "error: fail to read directory configuration file:"
                   << configFile;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"),
                           QString("Could not read directory configuration file \"%1\"")
                           .arg(dirConfigFileName));
        msgBox.setInformativeText(QString("Notebook directory \"%1\" may be corrupted").arg(path));
        msgBox.exec();
        return QJsonObject();
    }

    QByteArray configData = config.readAll();
    return QJsonDocument::fromJson(configData).object();
}

bool VDirectoryTree::writeDirectoryConfig(const QString &path, const QJsonObject &configJson)
{
    QString configFile = QDir(path).filePath(dirConfigFileName);

    qDebug() << "write config file:" << configFile;
    QFile config(configFile);
    if (!config.open(QIODevice::WriteOnly)) {
        qWarning() << "error: fail to open directory configuration file for write:"
                   << configFile;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"),
                           QString("Could not write directory configuration file \"%1\"")
                           .arg(dirConfigFileName));
        msgBox.exec();
        return false;
    }

    QJsonDocument configDoc(configJson);
    config.write(configDoc.toJson());
    return true;
}

bool VDirectoryTree::deleteDirectoryConfig(const QString &path)
{
    QString configFile = QDir(path).filePath(dirConfigFileName);

    QFile config(configFile);
    if (!config.remove()) {
        qWarning() << "error: fail to delete directory configuration file:"
                   << configFile;
        return false;
    }
    qDebug() << "delete config file:" << configFile;
    return true;
}

bool VDirectoryTree::validateDirConfigFile(const QJsonObject &configJson)
{
    if (configJson.isEmpty()) {
        return false;
    }
    if (!configJson.contains("version") || !configJson.contains("name")) {
        return false;
    }
    return true;
}

void VDirectoryTree::updateItemSubtree(QTreeWidgetItem *item)
{
    QJsonObject itemJson = item->data(0, Qt::UserRole).toJsonObject();
    Q_ASSERT(!itemJson.isEmpty());
    int nrChild = item->childCount();
    if (nrChild == 0) {
        updateDirectoryTreeOne(*item, 2);
    } else {
        for (int i = 0; i < nrChild; ++i) {
            QTreeWidgetItem *childItem = item->child(i);
            if (childItem->childCount() > 0) {
                continue;
            }
            updateDirectoryTreeOne(*childItem, 1);
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
    QString defaultDescription("");
    do {
        VNewDirDialog dialog(QString("Create a new directory under %1").arg(parentItemName), text,
                             defaultText, tr("&Description:"), defaultDescription, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            QString description = dialog.getDescriptionInput();
            if (isConflictNameWithChildren(parentItem, name)) {
                text = "Name already exists.\nPlease choose another name:";
                defaultText = name;
                defaultDescription = description;
                continue;
            }
            QTreeWidgetItem *newItem = createDirectoryAndUpdateTree(parentItem, name, description);
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
    QJsonObject curItemJson = curItem->data(0, Qt::UserRole).toJsonObject();
    QString curItemName = curItemJson["name"].toString();

    QString text("&Directory name:");
    QString defaultText("new_directory");
    QString defaultDescription("");
    do {
        VNewDirDialog dialog(QString("Create a new directory under %1").arg(curItemName), text,
                             defaultText, tr("&Description:"), defaultDescription, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            QString description = dialog.getDescriptionInput();
            if (isConflictNameWithChildren(curItem, name)) {
                text = "Name already exists.\nPlease choose another name:";
                defaultText = name;
                defaultDescription = description;
                continue;
            }
            QTreeWidgetItem *newItem = createDirectoryAndUpdateTree(curItem, name, description);
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
    QString defaultDescription("");
    do {
        VNewDirDialog dialog(tr("Create a new root directory"), text,
                             defaultText, tr("&Description:"), defaultDescription, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            QString description = dialog.getDescriptionInput();
            if (isConflictNameWithChildren(NULL, name)) {
                text = "Name already exists.\nPlease choose another name:";
                defaultText = name;
                defaultDescription = description;
                continue;
            }
            QTreeWidgetItem *newItem = createDirectoryAndUpdateTree(NULL, name, description);
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
    QJsonObject curItemJson = curItem->data(0, Qt::UserRole).toJsonObject();
    QString curItemName = curItemJson["name"].toString();

    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Are you sure you want to delete directory \"%1\"?")
                       .arg(curItemName));
    msgBox.setInformativeText(tr("This will delete any files under this directory."));
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Ok);
    if (msgBox.exec() == QMessageBox::Ok) {
        deleteDirectoryAndUpdateTree(curItem);
    }
}

QTreeWidgetItem* VDirectoryTree::createDirectoryAndUpdateTree(QTreeWidgetItem *parent,
                                                              const QString &name, const QString &description)
{
    QString relativePath("");
    QJsonObject parentJson;
    if (parent) {
        parentJson = parent->data(0, Qt::UserRole).toJsonObject();
        relativePath = QDir(parentJson["relative_path"].toString()).filePath(parentJson["name"].toString());
    }
    QString path = QDir(treePath).filePath(relativePath);
    QDir dir(path);
    if (!dir.mkdir(name)) {
        qDebug() << "warning: fail to create directory" << name << "under" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Could not create directory \"%1\" under \"%2\".")
                           .arg(name).arg(path));
        msgBox.setInformativeText(QString("Please check if there already exists a directory named \"%1\".").arg(name));
        msgBox.exec();
        return NULL;
    }

    QJsonObject configJson;
    configJson["version"] = "1";
    configJson["name"] = name;
    configJson["sub_directories"] = QJsonArray();
    configJson["files"] = QJsonArray();

    if (!writeDirectoryConfig(QDir(path).filePath(name), configJson)) {
        return NULL;
    }

    // Update parent's config file to include this new directory
    configJson = readDirectoryConfig(path);
    QJsonObject itemJson;
    itemJson["name"] = name;
    itemJson["description"] = description;
    QJsonArray subDirArray = configJson["sub_directories"].toArray();
    subDirArray.append(itemJson);
    configJson["sub_directories"] = subDirArray;
    if (!writeDirectoryConfig(path, configJson)) {
        deleteDirectoryConfig(QDir(path).filePath(name));
        dir.rmdir(name);
        return NULL;
    }

    return insertDirectoryTreeItem(parent, NULL, itemJson);
}

void VDirectoryTree::deleteDirectoryAndUpdateTree(QTreeWidgetItem *item)
{
    QJsonObject itemJson = item->data(0, Qt::UserRole).toJsonObject();
    QString itemName = itemJson["name"].toString();
    QString relativePath = itemJson["relative_path"].toString();

    // Update parent's config file to exclude this directory
    QString path = QDir(treePath).filePath(relativePath);
    QJsonObject configJson = readDirectoryConfig(path);
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
    if (!writeDirectoryConfig(path, configJson)) {
        qWarning() << "error: fail to update parent's configuration file to delete" << itemName;
        return;
    }

    // Delete the entire directory
    QString dirName = QDir(path).filePath(itemName);
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
