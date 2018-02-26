#include "vfile.h"

#include <QDir>
#include <QTextEdit>
#include <QFileInfo>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include "utils/vutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

const QString VFile::c_backupFileHeadMagic = "vnote_backup_file_826537664";

VFile::VFile(QObject *p_parent,
             const QString &p_name,
             FileType p_type,
             bool p_modifiable,
             QDateTime p_createdTimeUtc,
             QDateTime p_modifiedTimeUtc)
    : QObject(p_parent),
      m_name(p_name),
      m_opened(false),
      m_docType(VUtils::docTypeFromName(p_name)),
      m_type(p_type),
      m_modifiable(p_modifiable),
      m_createdTimeUtc(p_createdTimeUtc),
      m_modifiedTimeUtc(p_modifiedTimeUtc)
{
}

VFile::~VFile()
{
}

bool VFile::open()
{
    if (m_opened) {
        return true;
    }

    Q_ASSERT(!m_name.isEmpty());
    Q_ASSERT(m_content.isEmpty());

    QString filePath = fetchPath();
    Q_ASSERT(QFileInfo::exists(filePath));
    m_content = VUtils::readFileFromDisk(filePath);
    m_lastModified = QFileInfo(filePath).lastModified();
    m_opened = true;
    return true;
}

void VFile::close()
{
    if (!m_opened) {
        return;
    }

    m_content.clear();
    if (!m_backupName.isEmpty()) {
        VUtils::deleteFile(fetchBackupFilePath());
        m_backupName.clear();
    }

    m_opened = false;
}

bool VFile::save()
{
    Q_ASSERT(m_opened);
    Q_ASSERT(m_modifiable);

    bool ret = VUtils::writeFileToDisk(fetchPath(), m_content);
    if (ret) {
        m_lastModified = QFileInfo(fetchPath()).lastModified();
        m_modifiedTimeUtc = QDateTime::currentDateTimeUtc();
    }

    return ret;
}

QUrl VFile::getBaseUrl() const
{
    // Need to judge the path: Url, local file, resource file.
    QUrl baseUrl;
    QString basePath = fetchBasePath();
    QFileInfo pathInfo(basePath);
    if (pathInfo.exists()) {
        if (pathInfo.isNativePath()) {
            // Local file.
            baseUrl = QUrl::fromLocalFile(basePath + QDir::separator());
        } else {
            // Resource file.
            baseUrl = QUrl("qrc" + basePath + QDir::separator());
        }
    } else {
        // Url.
        baseUrl = QUrl(basePath + QDir::separator());
    }

    return baseUrl;
}

bool VFile::isInternalImageFolder(const QString &p_path) const
{
    return VUtils::equalPath(VUtils::basePathFromPath(p_path),
                             fetchBasePath())
           || VUtils::equalPath(p_path, fetchImageFolderPath());
}

bool VFile::isChangedOutside() const
{
    QDateTime lm = QFileInfo(fetchPath()).lastModified();
    return lm.toSecsSinceEpoch() != m_lastModified.toSecsSinceEpoch();
}

void VFile::reload()
{
    Q_ASSERT(m_opened);

    QString filePath = fetchPath();
    Q_ASSERT(QFileInfo::exists(filePath));
    m_content = VUtils::readFileFromDisk(filePath);
    m_lastModified = QFileInfo(filePath).lastModified();
}

QString VFile::backupFileOfPreviousSession() const
{
    Q_ASSERT(m_modifiable && m_backupName.isEmpty());

    QString basePath = QDir(fetchBasePath()).filePath(g_config->getBackupDirectory());
    QDir dir(basePath);

    QStringList files = getPotentialBackupFiles(basePath);
    foreach (const QString &file, files) {
        QString filePath = dir.filePath(file);
        if (isBackupFile(filePath)) {
            return filePath;
        }
    }

    return QString();
}

QString VFile::fetchBackupFilePath()
{
    QString basePath = QDir(fetchBasePath()).filePath(g_config->getBackupDirectory());
    QDir dir(basePath);

    if (m_backupName.isEmpty()) {
        m_backupName = VUtils::getFileNameWithSequence(basePath,
                                                       m_name + g_config->getBackupExtension(),
                                                       true);

        m_lastBackupFilePath = dir.filePath(m_backupName);
    } else {
        QString filePath = dir.filePath(m_backupName);
        if (filePath != m_lastBackupFilePath) {
            // File has been moved.
            // Delete the original backup file if it still exists.
            VUtils::deleteFile(m_lastBackupFilePath);

            m_lastBackupFilePath = filePath;
        }
    }

    return m_lastBackupFilePath;
}

QStringList VFile::getPotentialBackupFiles(const QString &p_dir) const
{
    QString nameFilter = QString("%1*%2").arg(m_name).arg(g_config->getBackupExtension());
    QStringList files = QDir(p_dir).entryList(QStringList(nameFilter),
                                              QDir::Files
                                              | QDir::Hidden
                                              | QDir::NoSymLinks
                                              | QDir::NoDotAndDotDot);
    return files;
}

bool VFile::isBackupFile(const QString &p_file) const
{
    QFile file(p_file);
    if (!file.open(QFile::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream st(&file);
    QString head = st.readLine();
    return head == fetchBackupFileHead();
}

QString VFile::fetchBackupFileHead() const
{
    return c_backupFileHeadMagic + " " + fetchPath();
}

bool VFile::writeBackupFile(const QString &p_content)
{
    return VUtils::writeFileToDisk(fetchBackupFilePath(),
                                   fetchBackupFileHead() + "\n" + p_content);
}

QString VFile::readBackupFile(const QString &p_file)
{
    const QString content = VUtils::readFileFromDisk(p_file);
    int idx = content.indexOf("\n");
    return content.mid(idx + 1);
}
