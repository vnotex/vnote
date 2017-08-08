#include "vdirectory.h"
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include "vconfigmanager.h"
#include "vfile.h"
#include "utils/vutils.h"

extern VConfigManager vconfig;

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

    V_ASSERT(m_subDirs.isEmpty() && m_files.isEmpty());

    QString path = retrivePath();
    QJsonObject configJson = VConfigManager::readDirectoryConfig(path);
    if (configJson.isEmpty()) {
        qWarning() << "invalid directory configuration in path" << path;
        return false;
    }

    // [sub_directories] section
    QJsonArray dirJson = configJson[DirConfig::c_subDirectories].toArray();
    for (int i = 0; i < dirJson.size(); ++i) {
        QJsonObject dirItem = dirJson[i].toObject();
        VDirectory *dir = new VDirectory(m_notebook, dirItem[DirConfig::c_name].toString(), this);
        m_subDirs.append(dir);
    }

    // [files] section
    QJsonArray fileJson = configJson[DirConfig::c_files].toArray();
    for (int i = 0; i < fileJson.size(); ++i) {
        QJsonObject fileItem = fileJson[i].toObject();
        VFile *file = new VFile(fileItem[DirConfig::c_name].toString(), this);
        m_files.append(file);
    }

    m_opened = true;
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

QString VDirectory::retriveBasePath() const
{
    return VUtils::basePathFromPath(retrivePath());
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

QJsonObject VDirectory::toConfigJson() const
{
    QJsonObject dirJson;
    dirJson[DirConfig::c_version] = "1";

    QJsonArray subDirs;
    for (int i = 0; i < m_subDirs.size(); ++i) {
        QJsonObject item;
        item[DirConfig::c_name] = m_subDirs[i]->getName();

        subDirs.append(item);
    }
    dirJson[DirConfig::c_subDirectories] = subDirs;

    QJsonArray files;
    for (int i = 0; i < m_files.size(); ++i) {
        QJsonObject item;
        item[DirConfig::c_name] = m_files[i]->getName();

        files.append(item);
    }
    dirJson[DirConfig::c_files] = files;

    return dirJson;
}

bool VDirectory::readConfig()
{
    return true;
}

bool VDirectory::writeToConfig() const
{
    QJsonObject json = toConfigJson();

    if (!getParentDirectory()) {
        // Root directory.
        addNotebookConfig(json);
    }

    qDebug() << "folder" << m_name << "write to config" << json;
    return writeToConfig(json);
}

bool VDirectory::writeToConfig(const QJsonObject &p_json) const
{
    return VConfigManager::writeDirectoryConfig(retrivePath(), p_json);
}

void VDirectory::addNotebookConfig(QJsonObject &p_json) const
{
    V_ASSERT(!getParentDirectory());

    QJsonObject nbJson = m_notebook->toConfigJsonNotebook();

    // Merge it to p_json.
    for (auto it = nbJson.begin(); it != nbJson.end(); ++it) {
        V_ASSERT(!p_json.contains(it.key()));
        p_json[it.key()] = it.value();
    }
}

VDirectory *VDirectory::createSubDirectory(const QString &p_name)
{
    Q_ASSERT(!p_name.isEmpty());
    // First open current directory
    if (!open()) {
        return NULL;
    }

    qDebug() << "create subfolder" << p_name << "in" << m_name;

    QString path = retrivePath();
    QDir dir(path);
    if (!dir.mkdir(p_name)) {
        qWarning() << "fail to create directory" << p_name << "under" << path;
        return NULL;
    }

    VDirectory *ret = new VDirectory(m_notebook, p_name, this);
    if (!ret->writeToConfig()) {
        dir.rmdir(p_name);
        delete ret;
        return NULL;
    }

    m_subDirs.append(ret);
    if (!writeToConfig()) {
        VConfigManager::deleteDirectoryConfig(QDir(path).filePath(p_name));
        dir.rmdir(p_name);
        delete ret;
        m_subDirs.removeLast();

        return NULL;
    }

    return ret;
}

VDirectory *VDirectory::findSubDirectory(const QString &p_name, bool p_caseSensitive)
{
    if (!open()) {
        return NULL;
    }

    QString name = p_caseSensitive ? p_name : p_name.toLower();
    for (int i = 0; i < m_subDirs.size(); ++i) {
        if (name == (p_caseSensitive ? m_subDirs[i]->getName() : m_subDirs[i]->getName().toLower())) {
            return m_subDirs[i];
        }
    }

    return NULL;
}

VFile *VDirectory::findFile(const QString &p_name, bool p_caseSensitive)
{
    if (!open()) {
        return NULL;
    }

    QString name = p_caseSensitive ? p_name : p_name.toLower();
    for (int i = 0; i < m_files.size(); ++i) {
        if (name == (p_caseSensitive ? m_files[i]->getName() : m_files[i]->getName().toLower())) {
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
    QFile file(QDir(path).filePath(p_name));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "fail to create file" << p_name;
        return NULL;
    }

    file.close();

    VFile *ret = new VFile(p_name, this);
    m_files.append(ret);
    if (!writeToConfig()) {
        file.remove();
        delete ret;
        m_files.removeLast();
        return NULL;
    }

    qDebug() << "note" << p_name << "created in folder" << m_name;

    return ret;
}

bool VDirectory::addFile(VFile *p_file, int p_index)
{
    if (!open()) {
        return false;
    }

    if (p_index == -1) {
        m_files.append(p_file);
    } else {
        m_files.insert(p_index, p_file);
    }

    if (!writeToConfig()) {
        if (p_index == -1) {
            m_files.removeLast();
        } else {
            m_files.remove(p_index);
        }

        return false;
    }

    p_file->setParent(this);

    qDebug() << "note" << p_file->getName() << "added to folder" << m_name;

    return true;
}

VFile *VDirectory::addFile(const QString &p_name, int p_index)
{
    if (!open() || p_name.isEmpty()) {
        return NULL;
    }

    VFile *file = new VFile(p_name, this);
    if (!file) {
        return NULL;
    }

    if (!addFile(file, p_index)) {
        delete file;
        return NULL;
    }

    return file;
}

bool VDirectory::addSubDirectory(VDirectory *p_dir, int p_index)
{
    if (!open()) {
        return false;
    }

    if (p_index == -1) {
        m_subDirs.append(p_dir);
    } else {
        m_subDirs.insert(p_index, p_dir);
    }

    if (!writeToConfig()) {
        if (p_index == -1) {
            m_subDirs.removeLast();
        } else {
            m_subDirs.remove(p_index);
        }

        return false;
    }

    p_dir->setParent(this);

    qDebug() << "folder" << p_dir->getName() << "added to folder" << m_name;

    return true;
}

VDirectory *VDirectory::addSubDirectory(const QString &p_name, int p_index)
{
    if (!open()) {
        return NULL;
    }

    VDirectory *dir = new VDirectory(m_notebook, p_name, this);
    if (!dir) {
        return NULL;
    }

    if (!addSubDirectory(dir, p_index)) {
        delete dir;
        return NULL;
    }

    return dir;
}

void VDirectory::deleteSubDirectory(VDirectory *p_subDir)
{
    QString dirPath = p_subDir->retrivePath();

    p_subDir->close();

    removeSubDirectory(p_subDir);

    // Delete the entire directory
    QDir dir(dirPath);
    if (!dir.removeRecursively()) {
        qWarning() << "fail to remove directory" << dirPath << "recursively";
    } else {
        qDebug() << "deleted" << dirPath << "from disk";
    }

    delete p_subDir;
}

bool VDirectory::removeSubDirectory(VDirectory *p_dir)
{
    V_ASSERT(m_opened);
    V_ASSERT(p_dir);

    int index = m_subDirs.indexOf(p_dir);
    V_ASSERT(index != -1);
    m_subDirs.remove(index);

    if (!writeToConfig()) {
        return false;
    }

    qDebug() << "folder" << p_dir->getName() << "removed from folder" << m_name;

    return true;
}

bool VDirectory::removeFile(VFile *p_file)
{
    V_ASSERT(m_opened);
    V_ASSERT(p_file);

    int index = m_files.indexOf(p_file);
    V_ASSERT(index != -1);
    m_files.remove(index);

    if (!writeToConfig()) {
        return false;
    }

    qDebug() << "note" << p_file->getName() << "removed from folder" << m_name;

    return true;
}

void VDirectory::deleteFile(VFile *p_file)
{
    removeFile(p_file);

    // Delete the file
    V_ASSERT(!p_file->isOpened());
    V_ASSERT(p_file->parent());

    p_file->deleteDiskFile();

    delete p_file;
}

bool VDirectory::rename(const QString &p_name)
{
    if (m_name == p_name) {
        return true;
    }

    QString oldName = m_name;

    VDirectory *parentDir = getParentDirectory();
    V_ASSERT(parentDir);
    // Rename it in disk.
    QDir dir(parentDir->retrivePath());
    if (!dir.rename(m_name, p_name)) {
        qWarning() << "fail to rename folder" << m_name << "to" << p_name << "in disk";
        return false;
    }

    m_name = p_name;

    // Update parent's config file
    if (!parentDir->writeToConfig()) {
        m_name = oldName;
        dir.rename(p_name, m_name);
        return false;
    }

    qDebug() << "folder renamed from" << oldName << "to" << m_name;

    return true;
}

VFile *VDirectory::copyFile(VDirectory *p_destDir, const QString &p_destName,
                            VFile *p_srcFile, bool p_cut)
{
    QString srcPath = QDir::cleanPath(p_srcFile->retrivePath());
    QString destPath = QDir::cleanPath(QDir(p_destDir->retrivePath()).filePath(p_destName));
    if (VUtils::equalPath(srcPath, destPath)) {
        return p_srcFile;
    }

    VDirectory *srcDir = p_srcFile->getDirectory();
    DocType docType = p_srcFile->getDocType();
    DocType newDocType = VUtils::docTypeFromName(destPath);

    QVector<ImageLink> images;
    if (docType == DocType::Markdown) {
        images = VUtils::fetchImagesFromMarkdownFile(p_srcFile,
                                                     ImageLink::LocalRelativeInternal);
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
        srcDir->removeFile(p_srcFile);

        p_srcFile->setName(p_destName);

        // Add the file to new dir's config
        if (p_destDir->addFile(p_srcFile, index)) {
            destFile = p_srcFile;
        } else {
            destFile = NULL;
        }
    } else {
        destFile = p_destDir->addFile(p_destName, -1);
    }

    if (!destFile) {
        return NULL;
    }

    if (docType != newDocType) {
        destFile->convert(docType, newDocType);
    }

    // We need to copy internal images when it is still markdown.
    if (!images.isEmpty()) {
        if (newDocType == DocType::Markdown) {
            QString parentPath = destFile->retriveBasePath();
            int nrPasted = 0;
            for (int i = 0; i < images.size(); ++i) {
                const ImageLink &link = images[i];
                if (!QFileInfo::exists(link.m_path)) {
                    continue;
                }

                QString errStr;
                bool ret = true;

                QString imageFolder = VUtils::directoryNameFromPath(VUtils::basePathFromPath(link.m_path));
                QString destImagePath = QDir(parentPath).filePath(imageFolder);
                ret = VUtils::makePath(destImagePath);
                if (!ret) {
                    errStr = tr("Fail to create image folder <span style=\"%1\">%2</span>.")
                               .arg(vconfig.c_dataTextStyle).arg(destImagePath);
                } else {
                    destImagePath = QDir(destImagePath).filePath(VUtils::fileNameFromPath(link.m_path));

                    // Copy or Cut the images accordingly.
                    if (VUtils::equalPath(destImagePath, link.m_path)) {
                        ret = false;
                    } else {
                        ret = VUtils::copyFile(link.m_path, destImagePath, p_cut);
                    }

                    if (ret) {
                        qDebug() << (p_cut ? "Cut" : "Copy") << "image"
                                 << link.m_path << "->" << destImagePath;

                        nrPasted++;
                    } else {
                        errStr = tr("Please check if there already exists a file <span style=\"%1\">%2</span> "
                                    "and then manually copy it and modify the note accordingly.")
                                   .arg(vconfig.c_dataTextStyle).arg(destImagePath);
                    }
                }

                if (!ret) {
                    VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                                        tr("Fail to copy image <span style=\"%1\">%2</span> while "
                                           "%5 note <span style=\"%3\">%4</span>.")
                                          .arg(vconfig.c_dataTextStyle).arg(link.m_path)
                                          .arg(vconfig.c_dataTextStyle).arg(srcPath)
                                          .arg(p_cut ? tr("moving") : tr("copying")),
                                        errStr, QMessageBox::Ok, QMessageBox::Ok, NULL);
                }
            }

            qDebug() << "pasted" << nrPasted << "images";
        } else {
            // Delete the images.
            int deleted = 0;
            for (int i = 0; i < images.size(); ++i) {
                QFile file(images[i].m_path);
                if (file.remove()) {
                    ++deleted;
                }
            }

            qDebug() << "delete" << deleted << "images since it is not Markdown any more for" << srcPath;
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
    if (VUtils::equalPath(srcPath, destPath)) {
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
        srcParentDir->removeSubDirectory(p_srcDir);

        p_srcDir->setName(p_destName);

        // Add the directory to new dir's config
        if (p_destDir->addSubDirectory(p_srcDir, index)) {
            destDir = p_srcDir;
        } else {
            destDir = NULL;
        }
    } else {
        destDir = p_destDir->addSubDirectory(p_destName, -1);
    }

    return destDir;
}

void VDirectory::setExpanded(bool p_expanded)
{
    if (p_expanded) {
        V_ASSERT(m_opened);
    }

    m_expanded = p_expanded;
}

void VDirectory::reorderFiles(int p_first, int p_last, int p_destStart)
{
    V_ASSERT(m_opened);
    V_ASSERT(p_first <= p_last);
    V_ASSERT(p_last < m_files.size());
    V_ASSERT(p_destStart < p_first || p_destStart > p_last);
    V_ASSERT(p_destStart >= 0 && p_destStart <= m_files.size());

    auto oriFiles = m_files;

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

    if (!writeToConfig()) {
        qWarning() << "fail to reorder files in config" << p_first << p_last << p_destStart;
        m_files = oriFiles;
    }
}

VFile *VDirectory::tryLoadFile(QStringList &p_filePath)
{
    qDebug() << "directory" << m_name << "tryLoadFile()" << p_filePath.join("/");
    if (p_filePath.isEmpty()) {
        return NULL;
    }

    bool opened = isOpened();
    if (!open()) {
        return NULL;
    }

    VFile *file = NULL;

#if defined(Q_OS_WIN)
    bool caseSensitive = false;
#else
    bool caseSensitive = true;
#endif

    if (p_filePath.size() == 1) {
        // File.
        file = findFile(p_filePath.at(0), caseSensitive);
    } else {
        // Directory.
        VDirectory *dir = findSubDirectory(p_filePath.at(0), caseSensitive);
        if (dir) {
            p_filePath.removeFirst();
            file = dir->tryLoadFile(p_filePath);
        }
    }

    if (!file && !opened) {
        close();
    }

    return file;
}
