#include "vnotebook.h"
#include <QDir>
#include <QDebug>
#include "vdirectory.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"
#include "vfile.h"

extern VConfigManager *g_config;

VNotebook::VNotebook(const QString &name, const QString &path, QObject *parent)
    : QObject(parent), m_name(name)
{
    m_path = QDir::cleanPath(path);
    m_rootDir = new VDirectory(this, VUtils::directoryNameFromPath(path));
}

VNotebook::~VNotebook()
{
    delete m_rootDir;
}

bool VNotebook::readConfig()
{
    QJsonObject configJson = VConfigManager::readDirectoryConfig(m_path);
    if (configJson.isEmpty()) {
        qWarning() << "fail to read notebook configuration" << m_path;
        return false;
    }

    // [image_folder] section.
    auto it = configJson.find(DirConfig::c_imageFolder);
    if (it != configJson.end()) {
        m_imageFolder = it.value().toString();
    }

    return true;
}

QJsonObject VNotebook::toConfigJsonNotebook() const
{
    QJsonObject json;

    // [image_folder] section.
    json[DirConfig::c_imageFolder] = m_imageFolder;

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

bool VNotebook::writeConfig() const
{
    QJsonObject json = toConfigJson();

    QJsonObject configJson = VConfigManager::readDirectoryConfig(m_path);
    if (configJson.isEmpty()) {
        qWarning() << "fail to read notebook configuration" << m_path;
        return false;
    }

    json[DirConfig::c_subDirectories] = configJson[DirConfig::c_subDirectories];
    json[DirConfig::c_files] = configJson[DirConfig::c_files];

    return VConfigManager::writeDirectoryConfig(m_path, json);
}

QString VNotebook::getName() const
{
    return m_name;
}

QString VNotebook::getPath() const
{
    return m_path;
}

void VNotebook::close()
{
    m_rootDir->close();
}

bool VNotebook::open()
{
    return m_rootDir->open();
}

VNotebook *VNotebook::createNotebook(const QString &p_name, const QString &p_path,
                                     bool p_import, const QString &p_imageFolder,
                                     QObject *p_parent)
{
    VNotebook *nb = new VNotebook(p_name, p_path, p_parent);

    // If @p_imageFolder is empty, it will report global configured folder as
    // its image folder.
    nb->setImageFolder(p_imageFolder);

    // Check if there alread exists a config file.
    if (p_import && VConfigManager::directoryConfigExist(p_path)) {
        qDebug() << "import existing notebook";
        nb->readConfig();
        return nb;
    }

    VUtils::makePath(p_path);

    if (!nb->writeToConfig()) {
        delete nb;
        return NULL;
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

        VDirectory *rootDir = p_notebook->getRootDir();
        QVector<VDirectory *> subdirs = rootDir->getSubDirs();
        for (auto dir : subdirs) {
            rootDir->deleteSubDirectory(dir);
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

VFile *VNotebook::tryLoadFile(const QString &p_path)
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

        VFile *file = m_rootDir->tryLoadFile(filePath);

        if (!file && !opened) {
            close();
        }

        return file;
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

bool VNotebook::isOpened() const
{
    return m_rootDir->isOpened();
}
