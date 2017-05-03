#include "vnotebook.h"
#include <QDir>
#include <QDebug>
#include "vdirectory.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"
#include "vfile.h"

VNotebook::VNotebook(const QString &name, const QString &path, QObject *parent)
    : QObject(parent), m_name(name), m_path(path)
{
    m_rootDir = new VDirectory(this, VUtils::directoryNameFromPath(path));
}

VNotebook::~VNotebook()
{
    delete m_rootDir;
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
                                     bool p_import, QObject *p_parent)
{
    VNotebook *nb = new VNotebook(p_name, p_path, p_parent);
    if (!nb) {
        return nb;
    }

    // Check if there alread exists a config file.
    if (p_import && VConfigManager::directoryConfigExist(p_path)) {
        qDebug() << "import existing notebook";
        return nb;
    }

    // Create directory config in @p_path
    QJsonObject configJson = VDirectory::createDirectoryJson();
    if (!VConfigManager::writeDirectoryConfig(p_path, configJson)) {
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
            qWarning() << "fail to delete notebook root directory" << rootDir->getName();
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
