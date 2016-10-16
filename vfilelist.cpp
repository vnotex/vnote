#include <QtDebug>
#include <QtWidgets>
#include "vfilelist.h"
#include "vconfigmanager.h"
#include "dialog/vnewfiledialog.h"

VFileList::VFileList(QWidget *parent)
    : QListWidget(parent)
{
    setContextMenuPolicy(Qt::CustomContextMenu);
    initActions();

    connect(this, &VFileList::customContextMenuRequested,
            this, &VFileList::contextMenuRequested);
    connect(this, &VFileList::itemClicked,
            this, &VFileList::handleItemClicked);
}

void VFileList::initActions()
{
    newFileAct = new QAction(tr("&New note"), this);
    newFileAct->setStatusTip(tr("Create a new note in current directory"));
    connect(newFileAct, &QAction::triggered,
            this, &VFileList::newFile);

    deleteFileAct = new QAction(tr("&Delete"), this);
    deleteFileAct->setStatusTip(tr("Delete selected note"));
    connect(deleteFileAct, &QAction::triggered,
            this, &VFileList::deleteFile);
}

void VFileList::setDirectory(QJsonObject dirJson)
{
    if (dirJson.isEmpty()) {
        clearDirectoryInfo();
        return;
    }

    directoryName = dirJson["name"].toString();
    rootPath = dirJson["root_path"].toString();
    relativePath = QDir(dirJson["relative_path"].toString()).filePath(directoryName);
    qDebug() << "FileList update:" << rootPath << relativePath << directoryName;

    updateFileList();
}

void VFileList::clearDirectoryInfo()
{
    directoryName = rootPath = relativePath = "";
    clear();
}

void VFileList::updateFileList()
{
    clear();
    QString path = QDir(rootPath).filePath(relativePath);

    if (!QDir(path).exists()) {
        qDebug() << "invalid notebook directory:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook directory."));
        msgBox.setInformativeText(QString("Notebook directory \"%1\" either does not exist or is not valid.")
                                  .arg(path));
        msgBox.exec();
        return;
    }

    QJsonObject configJson = VConfigManager::readDirectoryConfig(path);
    if (configJson.isEmpty()) {
        qDebug() << "invalid notebook configuration for directory:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook directory configuration."));
        msgBox.setInformativeText(QString("Notebook directory \"%1\" does not contain a valid configuration file.")
                                  .arg(path));
        msgBox.exec();
        return;
    }

    // Handle files section
    QJsonArray filesJson = configJson["files"].toArray();
    for (int i = 0; i < filesJson.size(); ++i) {
        QJsonObject fileItem = filesJson[i].toObject();
        insertFileListItem(fileItem);
    }
}

QListWidgetItem* VFileList::insertFileListItem(QJsonObject fileJson, bool atFront)
{
    Q_ASSERT(!fileJson.isEmpty());
    QListWidgetItem *item = new QListWidgetItem(fileJson["name"].toString());
    if (!fileJson["description"].toString().isEmpty()) {
        item->setToolTip(fileJson["description"].toString());
    }
    item->setData(Qt::UserRole, fileJson);

    if (atFront) {
        insertItem(0, item);
    } else {
        addItem(item);
    }
    qDebug() << "add new list item:" << fileJson["name"].toString();
    return item;
}

void VFileList::removeFileListItem(QListWidgetItem *item)
{
    // Qt ensures it will be removed from QListWidget automatically
    delete item;
}

void VFileList::newFile()
{
    QString text("&Note name:");
    QString defaultText("new_note");
    QString defaultDescription("");
    do {
        VNewFileDialog dialog(QString("Create a new note under %1").arg(directoryName), text,
                              defaultText, tr("&Description:"), defaultDescription, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            QString description = dialog.getDescriptionInput();
            if (isConflictNameWithExisting(name)) {
                text = "Name already exists.\nPlease choose another name:";
                defaultText = name;
                defaultDescription = description;
                continue;
            }
            QListWidgetItem *newItem = createFileAndUpdateList(name, description);
            if (newItem) {
                this->setCurrentItem(newItem);
            }
        }
        break;
    } while (true);
}

void VFileList::deleteFile()
{
    QListWidgetItem *curItem = currentItem();
    QJsonObject curItemJson = curItem->data(Qt::UserRole).toJsonObject();
    QString curItemName = curItemJson["name"].toString();

    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"),
                       QString("Are you sure you want to delete note \"%1\"?")
                       .arg(curItemName));
    msgBox.setInformativeText(tr("This may be not recoverable."));
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Ok);
    if (msgBox.exec() == QMessageBox::Ok) {
        deleteFileAndUpdateList(curItem);
    }
}

void VFileList::contextMenuRequested(QPoint pos)
{
    QListWidgetItem *item = itemAt(pos);
    QMenu menu(this);

    if (directoryName.isEmpty()) {
        return;
    }
    menu.addAction(newFileAct);
    if (item) {
        menu.addAction(deleteFileAct);
    }

    menu.exec(mapToGlobal(pos));
}

bool VFileList::isConflictNameWithExisting(const QString &name)
{
    int nrChild = this->count();
    for (int i = 0; i < nrChild; ++i) {
        QListWidgetItem *item = this->item(i);
        QJsonObject itemJson = item->data(Qt::UserRole).toJsonObject();
        Q_ASSERT(!itemJson.isEmpty());
        if (itemJson["name"].toString() == name) {
            return true;
        }
    }
    return false;
}

QListWidgetItem* VFileList::createFileAndUpdateList(const QString &name,
                                                    const QString &description)
{
    QString path = QDir(rootPath).filePath(relativePath);
    QString filePath = QDir(path).filePath(name);
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "error: fail to create file:" << filePath;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Could not create file \"%1\" under \"%2\".")
                           .arg(name).arg(path));
        msgBox.setInformativeText(QString("Please check if there already exists a file named \"%1\".").arg(name));
        msgBox.exec();
        return NULL;
    }
    file.close();
    qDebug() << "create file:" << filePath;

    // Update current directory's config file to include this new file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonObject fileJson;
    fileJson["name"] = name;
    fileJson["description"] = description;
    QJsonArray fileArray = dirJson["files"].toArray();
    fileArray.push_front(fileJson);
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        qWarning() << "error: fail to update directory's configuration file to add a new file"
                   << name;
        file.remove();
        return NULL;
    }

    return insertFileListItem(fileJson, true);
}

void VFileList::deleteFileAndUpdateList(QListWidgetItem *item)
{
    Q_ASSERT(item);
    QJsonObject itemJson = item->data(Qt::UserRole).toJsonObject();
    QString path = QDir(rootPath).filePath(relativePath);
    QString fileName = itemJson["name"].toString();
    QString filePath = QDir(path).filePath(fileName);

    // Update current directory's config file to exclude this file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonArray fileArray = dirJson["files"].toArray();
    bool deleted = false;
    for (int i = 0; i < fileArray.size(); ++i) {
        QJsonObject ele = fileArray[i].toObject();
        if (ele["name"].toString() == fileName) {
            fileArray.removeAt(i);
            deleted = true;
            break;
        }
    }
    if (!deleted) {
        qWarning() << "error: fail to find" << fileName << "to delete";
        return;
    }
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        qWarning() << "error: fail to update directory's configuration file to delete"
                   << fileName;
        return;
    }


    // Delete the file
    QFile file(filePath);
    if (!file.remove()) {
        qWarning() << "error: fail to delete" << filePath;
    } else {
        qDebug() << "delete" << filePath;
    }

    removeFileListItem(item);
}

void VFileList::handleItemClicked(QListWidgetItem *currentItem)
{
    if (!currentItem) {
        emit fileClicked(QJsonObject());
        return;
    }
    QJsonObject itemJson = currentItem->data(Qt::UserRole).toJsonObject();
    Q_ASSERT(!itemJson.isEmpty());
    itemJson["path"] = QDir::cleanPath(QDir(rootPath).filePath(relativePath));
    qDebug() << "click file:" << itemJson;
    emit fileClicked(itemJson);
}
