#include "vdirectory.h"
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include "vconfigmanager.h"
#include "vnotefile.h"
#include "utils/vutils.h"

extern VConfigManager *g_config;

VDirectory::VDirectory(VNotebook *p_notebook,
                       VDirectory *p_parent,
                       const QString &p_name,
                       QDateTime p_createdTimeUtc)
    : QObject(p_parent),
      m_notebook(p_notebook),
      m_name(p_name),
      m_opened(false),
      m_expanded(false),
      m_createdTimeUtc(p_createdTimeUtc)
{
}

bool VDirectory::open()
{
    if (m_opened) {
        return true;
    }

    V_ASSERT(m_subDirs.isEmpty() && m_files.isEmpty());

    QString path = fetchPath();
    QJsonObject configJson = VConfigManager::readDirectoryConfig(path);
    if (configJson.isEmpty()) {
        qWarning() << "invalid directory configuration in path" << path;
        return false;
    }

    // created_time
    m_createdTimeUtc = QDateTime::fromString(configJson[DirConfig::c_createdTime].toString(),
                                             Qt::ISODate);

    // [sub_directories] section
    QJsonArray dirJson = configJson[DirConfig::c_subDirectories].toArray();
    for (int i = 0; i < dirJson.size(); ++i) {
        QJsonObject dirItem = dirJson[i].toObject();
        VDirectory *dir = new VDirectory(m_notebook, this, dirItem[DirConfig::c_name].toString());
        m_subDirs.append(dir);
    }

    // [files] section
    QJsonArray fileJson = configJson[DirConfig::c_files].toArray();
    for (int i = 0; i < fileJson.size(); ++i) {
        QJsonObject fileItem = fileJson[i].toObject();
        VNoteFile *file = VNoteFile::fromJson(this,
                                              fileItem,
                                              FileType::Note,
                                              true);
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
        VNoteFile *file = m_files[i];
        file->close();
        delete file;
    }
    m_files.clear();

    m_opened = false;
}

QString VDirectory::fetchBasePath() const
{
    return VUtils::basePathFromPath(fetchPath());
}

QString VDirectory::fetchPath(const VDirectory *p_dir) const
{
    if (!p_dir) {
        return "";
    }
    VDirectory *parentDir = (VDirectory *)p_dir->parent();
    if (parentDir) {
        // Not the root directory
        return QDir(fetchPath(parentDir)).filePath(p_dir->getName());
    } else {
        return m_notebook->getPath();
    }
}

QString VDirectory::fetchRelativePath(const VDirectory *p_dir) const
{
    if (!p_dir) {
        return "";
    }
    VDirectory *parentDir = (VDirectory *)p_dir->parent();
    if (parentDir) {
        // Not the root directory
        return QDir(fetchRelativePath(parentDir)).filePath(p_dir->getName());
    } else {
        return "";
    }
}

QJsonObject VDirectory::toConfigJson() const
{
    QJsonObject dirJson;
    dirJson[DirConfig::c_version] = "1";
    dirJson[DirConfig::c_createdTime] = m_createdTimeUtc.toString(Qt::ISODate);

    QJsonArray subDirs;
    for (int i = 0; i < m_subDirs.size(); ++i) {
        QJsonObject item;
        item[DirConfig::c_name] = m_subDirs[i]->getName();

        subDirs.append(item);
    }
    dirJson[DirConfig::c_subDirectories] = subDirs;

    QJsonArray files;
    for (int i = 0; i < m_files.size(); ++i) {
        files.append(m_files[i]->toConfigJson());
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

bool VDirectory::updateFileConfig(const VNoteFile *p_file)
{
    Q_ASSERT(m_opened);
    Q_UNUSED(p_file);
    return writeToConfig();
}

bool VDirectory::writeToConfig(const QJsonObject &p_json) const
{
    return VConfigManager::writeDirectoryConfig(fetchPath(), p_json);
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

VDirectory *VDirectory::createSubDirectory(const QString &p_name,
                                           QString *p_errMsg)
{
    Q_ASSERT(!p_name.isEmpty());

    if (!open()) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to open folder %1.").arg(m_name));
        return NULL;
    }

    QDir dir(fetchPath());
    if (dir.exists(p_name)) {
        VUtils::addErrMsg(p_errMsg, tr("%1 already exists in directory %2.")
                                      .arg(p_name)
                                      .arg(fetchPath()));
        return NULL;
    }

    if (!dir.mkdir(p_name)) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to create folder in %1.")
                                      .arg(m_name));
        return NULL;
    }

    VDirectory *ret = new VDirectory(m_notebook,
                                     this,
                                     p_name,
                                     QDateTime::currentDateTimeUtc());
    if (!ret->writeToConfig()) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to write configuration of folder %1.")
                                      .arg(p_name));
        dir.rmdir(p_name);
        delete ret;
        return NULL;
    }

    m_subDirs.append(ret);
    if (!writeToConfig()) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to write configuration of folder %1.")
                                      .arg(p_name));

        QDir subdir(dir.filePath(p_name));
        subdir.removeRecursively();
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

VNoteFile *VDirectory::findFile(const QString &p_name, bool p_caseSensitive)
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

VNoteFile *VDirectory::createFile(const QString &p_name)
{
    Q_ASSERT(!p_name.isEmpty());
    if (!open()) {
        return NULL;
    }

    QString path = fetchPath();
    QFile file(QDir(path).filePath(p_name));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "fail to create file" << p_name;
        return NULL;
    }

    file.close();

    QDateTime dateTime = QDateTime::currentDateTimeUtc();
    VNoteFile *ret = new VNoteFile(this,
                                   p_name,
                                   FileType::Note,
                                   true,
                                   dateTime,
                                   dateTime);
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

bool VDirectory::addFile(VNoteFile *p_file, int p_index)
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

VNoteFile *VDirectory::addFile(const QString &p_name, int p_index)
{
    if (!open() || p_name.isEmpty()) {
        return NULL;
    }

    QDateTime dateTime = QDateTime::currentDateTimeUtc();
    VNoteFile *file = new VNoteFile(this,
                                    p_name,
                                    FileType::Note,
                                    true,
                                    dateTime,
                                    dateTime);
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
    if (!open() || p_name.isEmpty()) {
        return NULL;
    }

    VDirectory *dir = new VDirectory(m_notebook,
                                     this,
                                     p_name,
                                     QDateTime::currentDateTimeUtc());
    if (!dir) {
        return NULL;
    }

    if (!addSubDirectory(dir, p_index)) {
        delete dir;
        return NULL;
    }

    return dir;
}

bool VDirectory::deleteDirectory(bool p_skipRecycleBin, QString *p_errMsg)
{
    Q_ASSERT(!m_opened);
    Q_ASSERT(parent());

    // Delete the entire directory.
    bool ret = true;
    QString dirPath = fetchPath();
    if (!VUtils::deleteDirectory(m_notebook, dirPath, p_skipRecycleBin)) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to delete the directory %1.").arg(dirPath));
        ret = false;
    }

    return ret;
}

bool VDirectory::deleteDirectory(VDirectory *p_dir, bool p_skipRecycleBin, QString *p_errMsg)
{
    p_dir->close();

    bool ret = true;

    QString name = p_dir->getName();
    QString path = p_dir->fetchPath();

    if (!p_dir->deleteDirectory(p_skipRecycleBin, p_errMsg)) {
        ret = false;
    }

    VDirectory *paDir = p_dir->getParentDirectory();
    Q_ASSERT(paDir);
    if (!paDir->removeSubDirectory(p_dir)) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to remove the folder from the folder configuration."));
        ret = false;
    }

    delete p_dir;

    return ret;
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

    return true;
}

bool VDirectory::removeFile(VNoteFile *p_file)
{
    V_ASSERT(m_opened);
    V_ASSERT(p_file);

    int index = m_files.indexOf(p_file);
    V_ASSERT(index != -1);
    m_files.remove(index);

    if (!writeToConfig()) {
        return false;
    }

    return true;
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
    QDir dir(parentDir->fetchPath());
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

bool VDirectory::copyDirectory(VDirectory *p_destDir,
                               const QString &p_destName,
                               VDirectory *p_dir,
                               bool p_isCut,
                               VDirectory **p_targetDir,
                               QString *p_errMsg)
{
    bool ret = true;
    *p_targetDir = NULL;

    QString srcPath = QDir::cleanPath(p_dir->fetchPath());
    QString destPath = QDir::cleanPath(QDir(p_destDir->fetchPath()).filePath(p_destName));
    if (VUtils::equalPath(srcPath, destPath)) {
        *p_targetDir = p_dir;
        return false;
    }

    if (!p_destDir->isOpened()) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to open target folder."));
        return false;
    }

    QString opStr = p_isCut ? tr("cut") : tr("copy");
    VDirectory *paDir = p_dir->getParentDirectory();

    Q_ASSERT(paDir->isOpened());

    // Copy the directory.
    if (!VUtils::copyDirectory(srcPath, destPath, p_isCut)) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to %1 the folder.").arg(opStr));
        qWarning() << "fail to" << opStr << "the folder directory" << srcPath << "to" << destPath;
        return false;
    }

    // Add directory to VDirectory.
    VDirectory *destDir = NULL;
    if (p_isCut) {
        paDir->removeSubDirectory(p_dir);
        p_dir->setName(p_destName);
        // Add the directory to new dir's config
        if (p_destDir->addSubDirectory(p_dir, -1)) {
            destDir = p_dir;
        } else {
            destDir = NULL;
        }
    } else {
        destDir = p_destDir->addSubDirectory(p_destName, -1);
    }

    if (!destDir) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to add the folder to target folder's configuration."));
        return false;
    }

    qDebug() << "copyDirectory:" << p_dir << "to" << destDir;

    *p_targetDir = destDir;
    return ret;
}

void VDirectory::setExpanded(bool p_expanded)
{
    if (p_expanded) {
        V_ASSERT(m_opened);
    }

    m_expanded = p_expanded;
}

VNoteFile *VDirectory::tryLoadFile(QStringList &p_filePath)
{
    if (p_filePath.isEmpty()) {
        return NULL;
    }

    bool opened = isOpened();
    if (!open()) {
        return NULL;
    }

    VNoteFile *file = NULL;

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

VDirectory *VDirectory::tryLoadDirectory(QStringList &p_filePath)
{
    if (p_filePath.isEmpty()) {
        return NULL;
    }

    bool opened = isOpened();
    if (!open()) {
        return NULL;
    }

#if defined(Q_OS_WIN)
    bool caseSensitive = false;
#else
    bool caseSensitive = true;
#endif

    VDirectory *dir = findSubDirectory(p_filePath.at(0), caseSensitive);
    if (dir) {
        if (p_filePath.size() > 1) {
            p_filePath.removeFirst();
            dir = dir->tryLoadDirectory(p_filePath);
        }
    }

    if (!dir && !opened) {
        close();
    }

    return dir;
}

bool VDirectory::sortFiles(const QVector<int> &p_sortedIdx)
{
    V_ASSERT(m_opened);
    V_ASSERT(p_sortedIdx.size() == m_files.size());

    auto ori = m_files;

    for (int i = 0; i < p_sortedIdx.size(); ++i) {
        m_files[i] = ori[p_sortedIdx[i]];
    }

    bool ret = true;
    if (!writeToConfig()) {
        qWarning() << "fail to reorder files in config" << p_sortedIdx;
        m_files = ori;
        ret = false;
    }

    return ret;
}

bool VDirectory::sortSubDirectories(const QVector<int> &p_sortedIdx)
{
    V_ASSERT(m_opened);
    V_ASSERT(p_sortedIdx.size() == m_subDirs.size());

    auto ori = m_subDirs;

    for (int i = 0; i < p_sortedIdx.size(); ++i) {
        m_subDirs[i] = ori[p_sortedIdx[i]];
    }

    bool ret = true;
    if (!writeToConfig()) {
        qWarning() << "fail to reorder sub-directories in config" << p_sortedIdx;
        m_subDirs = ori;
        ret = false;
    }

    return ret;
}

QList<QString> VDirectory::collectFiles()
{
    QList<QString> files;
    bool opened = isOpened();
    if (!opened && !open()) {
        qWarning() << "fail to open directory" << fetchPath();
        return files;
    }

    // Files.
    for (auto const & file : m_files) {
        files.append(file->fetchPath());
    }

    // Subfolders.
    for (auto const & dir : m_subDirs) {
        files.append(dir->collectFiles());
    }

    if (!opened) {
        close();
    }

    return files;
}
