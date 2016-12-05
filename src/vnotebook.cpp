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
                                     QObject *p_parent)
{
    VNotebook *nb = new VNotebook(p_name, p_path, p_parent);
    if (!nb) {
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

void VNotebook::deleteNotebook(VNotebook *p_notebook)
{
    if (!p_notebook) {
        return;
    }
    QString path = p_notebook->getPath();

    p_notebook->close();
    delete p_notebook;

    QDir dir(path);
    if (!dir.removeRecursively()) {
        qWarning() << "failed to delete" << path;
    }
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
