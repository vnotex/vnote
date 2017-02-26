#include "vdirectory.h"
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include "vconfigmanager.h"
#include "vfile.h"
#include "utils/vutils.h"

VDirectory::VDirectory(VNotebook *p_notebook,
                       const QString &p_name, QObject *p_parent)
    : QObject(p_parent), m_notebook(p_notebook), m_name(p_name), m_opened(false),
      m_expanded(false)
{
}

bool VDirectory::open()
{
    if (m_opened) {
        return true;
    }
    Q_ASSERT(m_subDirs.isEmpty() && m_files.isEmpty());
    QString path = retrivePath();
    QJsonObject configJson = VConfigManager::readDirectoryConfig(path);
    if (configJson.isEmpty()) {
        qWarning() << "invalid directory configuration in path" << path;
        return false;
    }

    // [sub_directories] section
    QJsonArray dirJson = configJson["sub_directories"].toArray();
    for (int i = 0; i < dirJson.size(); ++i) {
        QJsonObject dirItem = dirJson[i].toObject();
        VDirectory *dir = new VDirectory(m_notebook, dirItem["name"].toString(), this);
        m_subDirs.append(dir);
    }

    // [files] section
    QJsonArray fileJson = configJson["files"].toArray();
    for (int i = 0; i < fileJson.size(); ++i) {
        QJsonObject fileItem = fileJson[i].toObject();
        VFile *file = new VFile(fileItem["name"].toString(), this);
        m_files.append(file);
    }
    m_opened = true;
    qDebug() << "dir" << m_name << "open" << m_subDirs.size() << "sub directories" << m_files.size() << "files";
    return true;
}

void VDirectory::close()
{
    if (!m_opened) {
        return;
    }

    for (int i = 0; i < m_subDirs.size(); ++i) {
        VDirectory *dir = m_subDirs[i];
        dir->close();
        delete dir;
    }
    m_subDirs.clear();

    for (int i = 0; i < m_files.size(); ++i) {
        VFile *file = m_files[i];
        file->close();
        delete file;
    }
    m_files.clear();

    m_opened = false;
}

QString VDirectory::retrivePath(const VDirectory *p_dir) const
{
    if (!p_dir) {
        return "";
    }
    VDirectory *parentDir = (VDirectory *)p_dir->parent();
    if (parentDir) {
        // Not the root directory
        return QDir(retrivePath(parentDir)).filePath(p_dir->getName());
    } else {
        return m_notebook->getPath();
    }
}

QString VDirectory::retriveRelativePath(const VDirectory *p_dir) const
{
    if (!p_dir) {
        return "";
    }
    VDirectory *parentDir = (VDirectory *)p_dir->parent();
    if (parentDir) {
        // Not the root directory
        return QDir(retriveRelativePath(parentDir)).filePath(p_dir->getName());
    } else {
        return "";
    }
}

QJsonObject VDirectory::createDirectoryJson()
{
    QJsonObject dirJson;
    dirJson["version"] = "1";
    dirJson["sub_directories"] = QJsonArray();
    dirJson["files"] = QJsonArray();
    return dirJson;
}

VDirectory *VDirectory::createSubDirectory(const QString &p_name)
{
    Q_ASSERT(!p_name.isEmpty());
    // First open current directory
    if (!open()) {
        return NULL;
    }
    QString path = retrivePath();
    QDir dir(path);
    if (!dir.mkdir(p_name)) {
        qWarning() << "fail to create directory" << p_name << "under" << path;
        return NULL;
    }

    QJsonObject subJson = createDirectoryJson();
    if (!VConfigManager::writeDirectoryConfig(QDir::cleanPath(QDir(path).filePath(p_name)), subJson)) {
        dir.rmdir(p_name);
        return NULL;
    }

    if (!createSubDirectoryInConfig(p_name)) {
        VConfigManager::deleteDirectoryConfig(QDir(path).filePath(p_name));
        dir.rmdir(p_name);
        return NULL;
    }

    VDirectory *ret = new VDirectory(m_notebook, p_name, this);
    m_subDirs.append(ret);
    return ret;
}

bool VDirectory::createSubDirectoryInConfig(const QString &p_name, int p_index)
{
    QString path = retrivePath();
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonObject itemJson;
    itemJson["name"] = p_name;
    QJsonArray subDirArray = dirJson["sub_directories"].toArray();
    if (p_index == -1) {
        subDirArray.append(itemJson);
    } else {
        subDirArray.insert(p_index, itemJson);
    }
    dirJson["sub_directories"] = subDirArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        return false;
    }
    return true;
}

VDirectory *VDirectory::findSubDirectory(const QString &p_name)
{
    if (!m_opened && !open()) {
        return NULL;
    }
    for (int i = 0; i < m_subDirs.size(); ++i) {
        if (p_name == m_subDirs[i]->getName()) {
            return m_subDirs[i];
        }
    }
    return NULL;
}

VFile *VDirectory::findFile(const QString &p_name)
{
    if (!m_opened && !open()) {
        return NULL;
    }
    for (int i = 0; i < m_files.size(); ++i) {
        if (p_name == m_files[i]->getName()) {
            return m_files[i];
        }
    }
    return NULL;
}

bool VDirectory::containsFile(const VFile *p_file) const
{
    if (!p_file) {
        return false;
    }
    QObject *paDir = p_file->parent();
    while (paDir) {
        if (paDir == this) {
            return true;
        } else {
            paDir = paDir->parent();
        }
    }
    return false;
}

VFile *VDirectory::createFile(const QString &p_name)
{
    Q_ASSERT(!p_name.isEmpty());
    if (!open()) {
        return NULL;
    }
    QString path = retrivePath();
    QString filePath = QDir(path).filePath(p_name);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "fail to create file" << p_name;
        return NULL;
    }
    file.close();
    qDebug() << "file created" << p_name;

    if (!createFileInConfig(p_name)) {
        file.remove();
        return NULL;
    }

    VFile *ret = new VFile(p_name, this);
    m_files.append(ret);
    return ret;
}

bool VDirectory::createFileInConfig(const QString &p_name, int p_index)
{
    QString path = retrivePath();
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonObject itemJson;
    itemJson["name"] = p_name;
    QJsonArray fileArray = dirJson["files"].toArray();
    if (p_index == -1) {
        fileArray.append(itemJson);
    } else {
        fileArray.insert(p_index, itemJson);
    }
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        return false;
    }
    return true;
}

VFile *VDirectory::addFile(VFile *p_file, int p_index)
{
    if (!open()) {
        return NULL;
    }
    if (!createFileInConfig(p_file->getName(), p_index)) {
        return NULL;
    }
    if (p_index == -1) {
        m_files.append(p_file);
    } else {
        m_files.insert(p_index, p_file);
    }
    p_file->setParent(this);
    return p_file;
}

VFile *VDirectory::addFile(const QString &p_name, int p_index)
{
    if (!open()) {
        return NULL;
    }
    if (!createFileInConfig(p_name, p_index)) {
        return NULL;
    }
    VFile *file = new VFile(p_name, this);
    if (!file) {
        return NULL;
    }
    if (p_index == -1) {
        m_files.append(file);
    } else {
        m_files.insert(p_index, file);
    }
    return file;
}

VDirectory *VDirectory::addSubDirectory(VDirectory *p_dir, int p_index)
{
    if (!open()) {
        return NULL;
    }
    if (!createSubDirectoryInConfig(p_dir->getName(), p_index)) {
        return NULL;
    }
    if (p_index == -1) {
        m_subDirs.append(p_dir);
    } else {
        m_subDirs.insert(p_index, p_dir);
    }
    p_dir->setParent(this);
    return p_dir;
}

VDirectory *VDirectory::addSubDirectory(const QString &p_name, int p_index)
{
    if (!open()) {
        return NULL;
    }
    if (!createSubDirectoryInConfig(p_name, p_index)) {
        return NULL;
    }
    VDirectory *dir = new VDirectory(m_notebook, p_name, this);
    if (!dir) {
        return NULL;
    }
    if (p_index == -1) {
        m_subDirs.append(dir);
    } else {
        m_subDirs.insert(p_index, dir);
    }
    return dir;
}

void VDirectory::deleteSubDirectory(VDirectory *p_subDir)
{
    QString dirPath = p_subDir->retrivePath();

    removeSubDirectory(p_subDir);

    // Delete the entire directory
    p_subDir->close();
    QDir dir(dirPath);
    if (!dir.removeRecursively()) {
        qWarning() << "fail to remove" << dirPath << "recursively";
    } else {
        qDebug() << "deleted" << dirPath;
    }
    delete p_subDir;
}

int VDirectory::removeSubDirectory(VDirectory *p_dir)
{
    Q_ASSERT(m_opened);
    Q_ASSERT(p_dir);

    QString path = retrivePath();

    int index;
    for (index = 0; index < m_subDirs.size(); ++index) {
        if (m_subDirs[index] == p_dir) {
            break;
        }
    }
    Q_ASSERT(index != m_subDirs.size());
    m_subDirs.remove(index);

    QString name = p_dir->getName();
    // Update config to exclude this directory
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(path);
    QJsonArray subDirArray = dirJson["sub_directories"].toArray();
    bool deleted = false;
    for (int i = 0; i < subDirArray.size(); ++i) {
        QJsonObject ele = subDirArray[i].toObject();
        if (ele["name"].toString() == name) {
            subDirArray.removeAt(i);
            deleted = true;
            index = i;
            break;
        }
    }
    Q_ASSERT(deleted);
    dirJson["sub_directories"] = subDirArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        qWarning() << "fail to update configuration in" << path;
    }
    return index;
}

// After calling this, p_file->parent() remain the same.
int VDirectory::removeFile(VFile *p_file)
{
    Q_ASSERT(m_opened);
    Q_ASSERT(p_file);

    QString path = retrivePath();

    int index;
    for (index = 0; index < m_files.size(); ++index) {
        if (m_files[index] == p_file) {
            break;
        }
    }
    Q_ASSERT(index != m_files.size());
    m_files.remove(index);
    QString name = p_file->getName();

    // Update config to exclude this file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(path);
    QJsonArray subFileArray = dirJson["files"].toArray();
    bool deleted = false;
    for (int i = 0; i < subFileArray.size(); ++i) {
        QJsonObject ele = subFileArray[i].toObject();
        if (ele["name"].toString() == name) {
            subFileArray.removeAt(i);
            deleted = true;
            index = i;
            break;
        }
    }
    Q_ASSERT(deleted);
    dirJson["files"] = subFileArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        qWarning() << "fail to update configuration in" << path;
    }
    return index;
}

void VDirectory::deleteFile(VFile *p_file)
{
    removeFile(p_file);

    // Delete the file
    Q_ASSERT(!p_file->isOpened());
    p_file->deleteDiskFile();
    delete p_file;
}

bool VDirectory::rename(const QString &p_name)
{
    if (m_name == p_name) {
        return true;
    }
    VDirectory *parentDir = getParentDirectory();
    Q_ASSERT(parentDir);
    QString parentPath = parentDir->retrivePath();
    QDir dir(parentPath);
    QString name = m_name;
    if (!dir.rename(m_name, p_name)) {
        qWarning() << "fail to rename directory" << m_name << "to" << p_name;
        return false;
    }
    m_name = p_name;

    // Update parent's config file
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(parentPath);
    QJsonArray subDirArray = dirJson["sub_directories"].toArray();
    int index = 0;
    for (index = 0; index < subDirArray.size(); ++index) {
        QJsonObject ele = subDirArray[index].toObject();
        if (ele["name"].toString() == name) {
            ele["name"] = p_name;
            subDirArray[index] = ele;
            break;
        }
    }
    Q_ASSERT(index != subDirArray.size());
    dirJson["sub_directories"] = subDirArray;
    if (!VConfigManager::writeDirectoryConfig(parentPath, dirJson)) {
        return false;
    }
    return true;
}

VFile *VDirectory::copyFile(VDirectory *p_destDir, const QString &p_destName,
                            VFile *p_srcFile, bool p_cut)
{
    QString srcPath = QDir::cleanPath(p_srcFile->retrivePath());
    QString destPath = QDir::cleanPath(QDir(p_destDir->retrivePath()).filePath(p_destName));
    if (srcPath == destPath) {
        return p_srcFile;
    }
    VDirectory *srcDir = p_srcFile->getDirectory();
    DocType docType = p_srcFile->getDocType();
    DocType newDocType = VUtils::isMarkdown(destPath) ? DocType::Markdown : DocType::Html;

    QVector<QString> images;
    if (docType == DocType::Markdown) {
        images = VUtils::imagesFromMarkdownFile(srcPath);
    }

    // Copy the file
    if (!VUtils::copyFile(srcPath, destPath, p_cut)) {
        return NULL;
    }

    // Handle VDirectory and VFile
    int index = -1;
    VFile *destFile = NULL;
    if (p_cut) {
        // Remove the file from config
        index = srcDir->removeFile(p_srcFile);
        p_srcFile->setName(p_destName);
        if (srcDir != p_destDir) {
            index = -1;
        }
        // Add the file to new dir's config
        destFile = p_destDir->addFile(p_srcFile, index);
    } else {
        destFile = p_destDir->addFile(p_destName, -1);
    }
    if (!destFile) {
        return NULL;
    }

    if (docType != newDocType) {
        destFile->convert(docType, newDocType);
    }

    // We need to copy images when it is still markdown
    if (!images.isEmpty()) {
        if (newDocType == DocType::Markdown) {
            QString dirPath = destFile->retriveImagePath();
            VUtils::makeDirectory(dirPath);
            int nrPasted = 0;
            for (int i = 0; i < images.size(); ++i) {
                if (!QFile(images[i]).exists()) {
                    continue;
                }

                QString destImagePath = QDir(dirPath).filePath(VUtils::fileNameFromPath(images[i]));
                // Copy or Cut the images accordingly.
                if (VUtils::copyFile(images[i], destImagePath, p_cut)) {
                    nrPasted++;
                } else {
                    VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                        QString("Fail to copy image %1.").arg(images[i]),
                                        tr("Please check if there already exists a file with the same name and then manually copy it."),
                                        QMessageBox::Ok, QMessageBox::Ok, NULL);
                }
            }
            qDebug() << "pasted" << nrPasted << "images sucessfully";
        } else {
            // Delete the images
            for (int i = 0; i < images.size(); ++i) {
                QFile file(images[i]);
                file.remove();
            }
        }
    }

    return destFile;
}

// Copy @p_srcDir to be a sub-directory of @p_destDir with name @p_destName.
VDirectory *VDirectory::copyDirectory(VDirectory *p_destDir, const QString &p_destName,
                                      VDirectory *p_srcDir, bool p_cut)
{
    QString srcPath = QDir::cleanPath(p_srcDir->retrivePath());
    QString destPath = QDir::cleanPath(QDir(p_destDir->retrivePath()).filePath(p_destName));
    if (srcPath == destPath) {
        return p_srcDir;
    }
    VDirectory *srcParentDir = p_srcDir->getParentDirectory();

    // Copy the directory
    if (!VUtils::copyDirectory(srcPath, destPath, p_cut)) {
        return NULL;
    }

    // Handle VDirectory
    int index = -1;
    VDirectory *destDir = NULL;
    if (p_cut) {
        // Remove the directory from config
        index = srcParentDir->removeSubDirectory(p_srcDir);
        p_srcDir->setName(p_destName);
        if (srcParentDir != p_destDir) {
            index = -1;
        }
        // Add the directory to new dir's config
        destDir = p_destDir->addSubDirectory(p_srcDir, index);
    } else {
        destDir = p_destDir->addSubDirectory(p_destName, -1);
    }
    return destDir;
}

void VDirectory::setExpanded(bool p_expanded)
{
    if (p_expanded) {
        Q_ASSERT(m_opened);
    }
    m_expanded = p_expanded;
}

void VDirectory::reorderFiles(int p_first, int p_last, int p_destStart)
{
    Q_ASSERT(m_opened);
    Q_ASSERT(p_first <= p_last);
    Q_ASSERT(p_last < m_files.size());
    Q_ASSERT(p_destStart < p_first || p_destStart > p_last);
    Q_ASSERT(p_destStart >= 0 && p_destStart <= m_files.size());

    if (!reorderFilesInConfig(p_first, p_last, p_destStart)) {
        qWarning() << "fail to reorder files in config" << p_first << p_last << p_destStart;
        return;
    }

    // Reorder m_files.
    if (p_destStart > p_last) {
        int to = p_destStart - 1;
        for (int i = p_first; i <= p_last; ++i) {
            // Move p_first to p_destStart every time.
            m_files.move(p_first, to);
        }
    } else {
        int to = p_destStart;
        for (int i = p_first; i <= p_last; ++i) {
            m_files.move(i, to++);
        }
    }
}

bool VDirectory::reorderFilesInConfig(int p_first, int p_last, int p_destStart)
{
    QString path = retrivePath();
    QJsonObject dirJson = VConfigManager::readDirectoryConfig(path);
    Q_ASSERT(!dirJson.isEmpty());
    QJsonArray fileArray = dirJson["files"].toArray();
    Q_ASSERT(fileArray.size() == m_files.size());

    if (p_destStart > p_last) {
        int to = p_destStart - 1;
        for (int i = p_first; i <= p_last; ++i) {
            // Move p_first to p_destStart every time.
            QJsonValue ele = fileArray.takeAt(p_first);
            fileArray.insert(to, ele);
        }
    } else {
        int to = p_destStart;
        for (int i = p_first; i <= p_last; ++i) {
            QJsonValue ele = fileArray.takeAt(i);
            fileArray.insert(to++, ele);
        }
    }
    dirJson["files"] = fileArray;
    if (!VConfigManager::writeDirectoryConfig(path, dirJson)) {
        return false;
    }
    return true;
}
