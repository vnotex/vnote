#include <QtDebug>
#include <QtWidgets>
#include "vfilelist.h"
#include "vconfigmanager.h"
#include "dialog/vnewfiledialog.h"
#include "vnote.h"

VFileList::VFileList(VNote *vnote, QWidget *parent)
    : QListWidget(parent), vnote(vnote)
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
    connect(newFileAct, SIGNAL(triggered(bool)),
            this, SLOT(newFile()));

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

    notebook = dirJson["notebook"].toString();
    relativePath = dirJson["relative_path"].toString();
    rootPath = "";
    const QVector<VNotebook> &notebooks = vnote->getNotebooks();
    for (int i = 0; i < notebooks.size(); ++i) {
        if (notebooks[i].getName() == notebook) {
            rootPath = notebooks[i].getPath();
            break;
        }
    }
    Q_ASSERT(!rootPath.isEmpty());

    updateFileList();
}

void VFileList::clearDirectoryInfo()
{
    notebook = relativePath = rootPath = "";
}

void VFileList::updateFileList()
{
    clear();
    QString path = QDir(rootPath).filePath(relativePath);

    if (!QDir(path).exists()) {
        qDebug() << "invalid notebook directory:" << path;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), tr("Invalid notebook directory."),
                           QMessageBox::Ok, this);
        msgBox.setInformativeText(QString("Notebook directory \"%1\" either does not exist or is not valid.")
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
    item->setData(Qt::UserRole, fileJson);

    if (atFront) {
        insertItem(0, item);
    } else {
        addItem(item);
    }
    // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
    update();
    qDebug() << "add new list item:" << fileJson["name"].toString();
    return item;
}

void VFileList::removeFileListItem(QListWidgetItem *item)
{
    setCurrentRow(-1);
    removeItemWidget(item);
    delete item;
    // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
    update();
}

void VFileList::newFile()
{
    QString text("&Note name:");
    QString defaultText("new_note");
    do {
        VNewFileDialog dialog(QString("Create a new note under %1").arg(getDirectoryName()), text,
                              defaultText, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (isConflictNameWithExisting(name)) {
                text = "Name already exists.\nPlease choose another name:";
                defaultText = name;
                continue;
            }
            QListWidgetItem *newItem = createFileAndUpdateList(name);
            if (newItem) {
                setCurrentItem(newItem);
                // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
                update();

                // Open this file in edit mode
                QJsonObject itemJson = newItem->data(Qt::UserRole).toJsonObject();
                Q_ASSERT(!itemJson.isEmpty());
                itemJson["notebook"] = notebook;
                itemJson["relative_path"] = QDir::cleanPath(QDir(relativePath).filePath(name));
                itemJson["mode"] = OpenFileMode::Edit;
                emit fileCreated(itemJson);
            }
        }
        break;
    } while (true);
}

void VFileList::deleteFile()
{
    QListWidgetItem *curItem = currentItem();
    Q_ASSERT(curItem);
    QJsonObject curItemJson = curItem->data(Qt::UserRole).toJsonObject();
    QString curItemName = curItemJson["name"].toString();

    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"),
                       QString("Are you sure you want to delete note \"%1\"?")
                       .arg(curItemName), QMessageBox::Ok | QMessageBox::Cancel,
                       this);
    msgBox.setInformativeText(tr("This may be not recoverable."));
    msgBox.setDefaultButton(QMessageBox::Ok);
    if (msgBox.exec() == QMessageBox::Ok) {
        // First close this file forcely
        curItemJson["notebook"] = notebook;
        curItemJson["relative_path"] = QDir::cleanPath(QDir(relativePath).filePath(curItemName));
        emit fileDeleted(curItemJson);

        deleteFileAndUpdateList(curItem);
    }
}

void VFileList::contextMenuRequested(QPoint pos)
{
    QListWidgetItem *item = itemAt(pos);
    QMenu menu(this);

    if (notebook.isEmpty()) {
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

QListWidgetItem* VFileList::createFileAndUpdateList(const QString &name)
{
    QString path = QDir(rootPath).filePath(relativePath);
    QString filePath = QDir(path).filePath(name);
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "error: fail to create file:" << filePath;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Could not create file \"%1\" under \"%2\".")
                           .arg(name).arg(path), QMessageBox::Ok, this);
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
    // Qt seems not to update the QListWidget correctly. Manually force it to repaint.
    update();
    QJsonObject itemJson = currentItem->data(Qt::UserRole).toJsonObject();
    Q_ASSERT(!itemJson.isEmpty());
    itemJson["notebook"] = notebook;
    itemJson["relative_path"] = QDir::cleanPath(QDir(relativePath).filePath(itemJson["name"].toString()));
    itemJson["mode"] = OpenFileMode::Read;
    emit fileClicked(itemJson);
}

bool VFileList::importFile(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    if (isConflictNameWithExisting(name)) {
        return false;
    }

    // Copy file @name to current directory
    QString targetPath = QDir(rootPath).filePath(relativePath);
    QString srcName = QFileInfo(name).fileName();
    if (srcName.isEmpty()) {
        return false;
    }
    QString targetName = QDir(targetPath).filePath(srcName);

    bool ret = QFile::copy(name, targetName);
    if (!ret) {
        qWarning() << "error: fail to copy" << name << "to" << targetName;
        return false;
    }

    // Update current directory's config file to include this new file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(targetPath);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonObject fileJson;
    fileJson["name"] = srcName;
    QJsonArray fileArray = dirJson["files"].toArray();
    fileArray.push_front(fileJson);
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(targetPath, dirJson)) {
        qWarning() << "error: fail to update directory's configuration file to add a new file"
                   << srcName;
        QFile(targetName).remove();
        return false;
    }

    return insertFileListItem(fileJson, true);
}

void VFileList::handleNotebookRenamed(const QVector<VNotebook> &notebooks,
                                      const QString &oldName, const QString &newName)
{
    if (oldName == notebook) {
        // Update treePath (though treePath actually will not be changed)
        notebook = newName;
        rootPath.clear();
        const QVector<VNotebook> &notebooks = vnote->getNotebooks();
        for (int i = 0; i < notebooks.size(); ++i) {
            if (notebooks[i].getName() == notebook) {
                rootPath = notebooks[i].getPath();
                break;
            }
        }
        Q_ASSERT(!rootPath.isEmpty());
    }
}

// FIXME: when @oldRelativePath is part of relativePath, we also need to update relativePath partialy
void VFileList::handleDirectoryRenamed(const QString &notebook,
                                       const QString &oldRelativePath, const QString &newRelativePath)
{
    if (notebook == this->notebook
        && oldRelativePath == relativePath) {
        relativePath = newRelativePath;
    }
}
