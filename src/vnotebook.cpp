#include "vnotebook.h"
#include <QDir>
#include <QDebug>
#include <QCoreApplication>

#include "vdirectory.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"
#include "vnotefile.h"

extern VConfigManager *g_config;

VNotebook::VNotebook(const QString &name, const QString &path, QObject *parent)
    : QObject(parent), m_name(name), m_valid(false)
{
    setPath(path);
    m_recycleBinFolder = g_config->getRecycleBinFolder();
    m_rootDir = new VDirectory(this,
                               NULL,
                               VUtils::directoryNameFromPath(m_path),
                               QDateTime::currentDateTimeUtc());
}

VNotebook::~VNotebook()
{
    delete m_rootDir;
}

void VNotebook::setPath(const QString &p_path)
{
    m_pathInConfig = QDir::cleanPath(p_path);
    if (QDir::isAbsolutePath(m_pathInConfig)) {
        m_path = m_pathInConfig;
    } else {
        m_path = QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(m_pathInConfig));
    }
}

bool VNotebook::readConfigNotebook()
{
    QJsonObject configJson = VConfigManager::readDirectoryConfig(m_path);
    if (configJson.isEmpty()) {
        qWarning() << "fail to read notebook configuration" << m_path;
        m_valid = false;
        return false;
    }

    // [image_folder] section.
    auto it = configJson.find(DirConfig::c_imageFolder);
    if (it != configJson.end()) {
        m_imageFolder = it.value().toString();
    }

    // [recycle_bin_folder] section.
    it = configJson.find(DirConfig::c_recycleBinFolder);
    if (it != configJson.end()) {
        m_recycleBinFolder = it.value().toString();
    }

    // [attachment_folder] section.
    // SHOULD be processed at last.
    it = configJson.find(DirConfig::c_attachmentFolder);
    if (it != configJson.end()) {
        m_attachmentFolder = it.value().toString();
    }

    // We do not allow empty attachment folder.
    if (m_attachmentFolder.isEmpty()) {
        m_attachmentFolder = g_config->getAttachmentFolder();
        Q_ASSERT(!m_attachmentFolder.isEmpty());
        writeConfigNotebook();
    }

    m_valid = true;
    return true;
}

QJsonObject VNotebook::toConfigJsonNotebook() const
{
    QJsonObject json;

    // [image_folder] section.
    json[DirConfig::c_imageFolder] = m_imageFolder;

    // [attachment_folder] section.
    json[DirConfig::c_attachmentFolder] = m_attachmentFolder;

    // [recycle_bin_folder] section.
    json[DirConfig::c_recycleBinFolder] = m_recycleBinFolder;

    return json;
}

QJsonObject VNotebook::toConfigJson() const
{
    QJsonObject json = m_rootDir->toConfigJson();
    QJsonObject nbJson = toConfigJsonNotebook();

    // Merge nbJson to json.
    for (auto it = nbJson.begin(); it != nbJson.end(); ++it) {
        V_ASSERT(!json.contains(it.key()));
        json[it.key()] = it.value();
    }

    return json;
}

bool VNotebook::writeToConfig() const
{
    return VConfigManager::writeDirectoryConfig(m_path, toConfigJson());
}

bool VNotebook::writeConfigNotebook() const
{
    QJsonObject nbJson = toConfigJsonNotebook();

    QJsonObject configJson = VConfigManager::readDirectoryConfig(m_path);
    if (configJson.isEmpty()) {
        qWarning() << "fail to read notebook configuration" << m_path;
        return false;
    }

    for (auto it = nbJson.begin(); it != nbJson.end(); ++it) {
        configJson[it.key()] = it.value();
    }

    return VConfigManager::writeDirectoryConfig(m_path, configJson);
}

void VNotebook::close()
{
    m_rootDir->close();
}

bool VNotebook::open()
{
    if (!m_valid) {
        return false;
    }

    QString recycleBinPath = getRecycleBinFolderPath();
    if (!QFileInfo::exists(recycleBinPath)) {
        QDir dir(m_path);
        if (!dir.mkpath(recycleBinPath)) {
            qWarning() << "fail to create recycle bin folder" << recycleBinPath
                       << "for notebook" << m_name;
            return false;
        }
    }

    return m_rootDir->open();
}

VNotebook *VNotebook::createNotebook(const QString &p_name,
                                     const QString &p_path,
                                     bool p_import,
                                     const QString &p_imageFolder,
                                     const QString &p_attachmentFolder,
                                     QObject *p_parent)
{
    VNotebook *nb = new VNotebook(p_name, p_path, p_parent);

    // If @p_imageFolder is empty, it will report global configured folder as
    // its image folder.
    nb->setImageFolder(p_imageFolder);

    // If @p_attachmentFolder is empty, use global configured folder.
    QString attachmentFolder = p_attachmentFolder;
    if (attachmentFolder.isEmpty()) {
        attachmentFolder = g_config->getAttachmentFolder();
    }

    nb->setAttachmentFolder(attachmentFolder);

    // Check if there alread exists a config file.
    if (p_import && VConfigManager::directoryConfigExist(nb->getPath())) {
        qDebug() << "import existing notebook";
        nb->readConfigNotebook();
        return nb;
    }

    VUtils::makePath(nb->getPath());

    if (!nb->writeToConfig()) {
        delete nb;
        return NULL;
    } else {
        nb->m_valid = true;
    }

    return nb;
}

bool VNotebook::deleteNotebook(VNotebook *p_notebook, bool p_deleteFiles)
{
    bool ret = true;

    if (!p_notebook) {
        return true;
    }

    if (p_deleteFiles) {
        if (!p_notebook->open()) {
            qWarning() << "fail to open notebook" << p_notebook->getName()
                       << "to delete";
            ret = false;
            goto exit;
        }

        // Delete sub directories.
        VDirectory *rootDir = p_notebook->getRootDir();
        QVector<VDirectory *> subdirs = rootDir->getSubDirs();
        for (auto dir : subdirs) {
            // Skip recycle bin.
            VDirectory::deleteDirectory(dir, true);
        }

        // Delete the recycle bin.
        QDir recycleDir(p_notebook->getRecycleBinFolderPath());
        if (!recycleDir.removeRecursively()) {
            qWarning() << "fail to delete notebook recycle bin folder"
                       << p_notebook->getRecycleBinFolderPath();
            ret = false;
        }

        // Delete the config file.
        if (!VConfigManager::deleteDirectoryConfig(p_notebook->getPath())) {
            ret = false;
            goto exit;
        }

        // If it is now an empty directory, delete it.
        QDir dir(p_notebook->getPath());
        dir.cdUp();
        if (!dir.rmdir(rootDir->getName())) {
            qWarning() << "fail to delete notebook root folder" << rootDir->getName();
            ret = false;
        }
    }

exit:
    p_notebook->close();
    delete p_notebook;

    return ret;
}

void VNotebook::rename(const QString &p_name)
{
    if (p_name == m_name || p_name.isEmpty()) {
        return;
    }

    m_name = p_name;
}

bool VNotebook::containsFile(const VFile *p_file) const
{
    return m_rootDir->containsFile(p_file);
}

VNoteFile *VNotebook::tryLoadFile(const QString &p_path)
{
    QFileInfo fi(p_path);
    Q_ASSERT(fi.isAbsolute());
    if (!fi.exists()) {
        return NULL;
    }

    QStringList filePath;
    if (VUtils::splitPathInBasePath(m_path, p_path, filePath)) {
        if (filePath.isEmpty()) {
            return NULL;
        }

        bool opened = isOpened();
        if (!open()) {
            return NULL;
        }

        VNoteFile *file = m_rootDir->tryLoadFile(filePath);

        if (!file && !opened) {
            close();
        }

        return file;
    }

    return NULL;
}

VDirectory *VNotebook::tryLoadDirectory(const QString &p_path)
{
    QFileInfo fi(p_path);
    Q_ASSERT(fi.isAbsolute());
    if (!fi.exists()) {
        return NULL;
    }

    QStringList filePath;
    if (VUtils::splitPathInBasePath(m_path, p_path, filePath)) {
        if (filePath.isEmpty()) {
            return NULL;
        }

        bool opened = isOpened();
        if (!open()) {
            return NULL;
        }

        VDirectory *dir = m_rootDir->tryLoadDirectory(filePath);

        if (!dir && !opened) {
            close();
        }

        return dir;
    }

    return NULL;
}

const QString &VNotebook::getImageFolder() const
{
    if (m_imageFolder.isEmpty()) {
        return g_config->getImageFolder();
    } else {
        return m_imageFolder;
    }
}

void VNotebook::setImageFolder(const QString &p_imageFolder)
{
    m_imageFolder = p_imageFolder;
}

const QString &VNotebook::getImageFolderConfig() const
{
    return m_imageFolder;
}

const QString &VNotebook::getAttachmentFolder() const
{
    return m_attachmentFolder;
}

void VNotebook::setAttachmentFolder(const QString &p_attachmentFolder)
{
    m_attachmentFolder = p_attachmentFolder;
}

bool VNotebook::isOpened() const
{
    return m_rootDir->isOpened();
}

QDateTime VNotebook::getCreatedTimeUtc()
{
    if (!isOpened()) {
        if (!open()) {
            return QDateTime();
        }
    }

    return m_rootDir->getCreatedTimeUtc();
}

QString VNotebook::getRecycleBinFolderPath() const
{
    QFileInfo fi(m_recycleBinFolder);
    if (fi.isAbsolute()) {
        return m_recycleBinFolder;
    } else {
        return QDir(m_path).filePath(m_recycleBinFolder);
    }
}

QList<QString> VNotebook::collectFiles()
{
    QList<QString> files;

    bool opened = isOpened();
    if (!opened && !open()) {
        qWarning() << "fail to open notebook %1" << m_path;
        return files;
    }

    files = m_rootDir->collectFiles();

    if (!opened) {
        close();
    }

    return files;
}

void VNotebook::updatePath(const QString &p_path)
{
    Q_ASSERT(!isOpened());
    m_valid = false;
    setPath(p_path);
    delete m_rootDir;
    m_rootDir = new VDirectory(this,
                               NULL,
                               VUtils::directoryNameFromPath(m_path),
                               QDateTime::currentDateTimeUtc());

    readConfigNotebook();
}
