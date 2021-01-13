#include "buffer.h"

#include <QTimer>

#include <notebook/node.h>
#include <utils/fileutils.h>
#include <widgets/viewwindow.h>
#include <utils/pathutils.h>

#include <core/configmgr.h>
#include <core/editorconfig.h>

#include "bufferprovider.h"
#include "exception.h"

using namespace vnotex;

static vnotex::ID generateBufferID()
{
    static vnotex::ID id = 0;
    return ++id;
}

Buffer::Buffer(const BufferParameters &p_parameters,
               QObject *p_parent)
    : QObject(p_parent),
      m_provider(p_parameters.m_provider),
      c_id(generateBufferID()),
      m_readOnly(m_provider->isReadOnly())
{
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(1000);
    connect(m_autoSaveTimer, &QTimer::timeout,
            this, &Buffer::autoSave);

    readContent();

    checkBackupFileOfPreviousSession();
}

Buffer::~Buffer()
{
    Q_ASSERT(m_attachedViewWindowCount == 0);
    Q_ASSERT(!m_viewWindowToSync);
    Q_ASSERT(!isModified());
    Q_ASSERT(m_backupFilePath.isEmpty());
}

int Buffer::getAttachViewWindowCount() const
{
    return m_attachedViewWindowCount;
}

void Buffer::attachViewWindow(ViewWindow *p_win)
{
    Q_UNUSED(p_win);
    Q_ASSERT(!(m_state & StateFlag::Discarded));
    ++m_attachedViewWindowCount;
}

void Buffer::detachViewWindow(ViewWindow *p_win)
{
    Q_ASSERT(p_win != m_viewWindowToSync);

    --m_attachedViewWindowCount;
    Q_ASSERT(m_attachedViewWindowCount >= 0);

    if (m_attachedViewWindowCount == 0) {
        emit attachedViewWindowEmpty();
    }
}

ViewWindow *Buffer::createViewWindow(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent)
{
    auto window = createViewWindowInternal(p_paras, p_parent);
    Q_ASSERT(window);
    window->attachToBuffer(this);
    return window;
}

bool Buffer::match(const Node *p_node) const
{
    Q_ASSERT(p_node);
    return m_provider->match(p_node);
}

bool Buffer::match(const QString &p_filePath) const
{
    return m_provider->match(p_filePath);
}

QString Buffer::getName() const
{
    return m_provider->getName();
}

QString Buffer::getPath() const
{
    return m_provider->getPath();
}

QString Buffer::getContentPath() const
{
    return m_provider->getContentPath();
}

QString Buffer::getResourcePath() const
{
    return m_provider->getResourcePath();
}

ID Buffer::getID() const
{
    return c_id;
}

const QString &Buffer::getContent() const
{
    const_cast<Buffer *>(this)->syncContent();
    return m_content;
}

void Buffer::setContent(const QString &p_content, int &p_revision)
{
    m_viewWindowToSync = nullptr;
    m_content = p_content;
    p_revision = ++m_revision;
    setModified(true);
    m_autoSaveTimer->start();
    emit contentsChanged();
}

void Buffer::invalidateContent(const ViewWindow *p_win,
                               const std::function<void(int)> &p_setRevision)
{
    Q_ASSERT(!m_viewWindowToSync || m_viewWindowToSync == p_win);
    ++m_revision;
    p_setRevision(m_revision);
    m_viewWindowToSync = p_win;
    m_autoSaveTimer->start();
    emit contentsChanged();
}

int Buffer::getRevision() const
{
    return m_revision;
}

void Buffer::syncContent(const ViewWindow *p_win)
{
    if (m_viewWindowToSync == p_win) {
        syncContent();
    }
}

void Buffer::syncContent()
{
    if (m_viewWindowToSync) {
        // Need to sync content.
        m_content = m_viewWindowToSync->getLatestContent();
        m_viewWindowToSync = nullptr;
    }
}

bool Buffer::isModified() const
{
    return m_modified;
}

void Buffer::setModified(bool p_modified)
{
    if (m_modified == p_modified) {
        return;
    }

    m_modified = p_modified;
    emit modified(m_modified);
}

bool Buffer::isReadOnly() const
{
    return m_readOnly;
}

Buffer::OperationCode Buffer::save(bool p_force)
{
    Q_ASSERT(!m_readOnly);
    if (m_readOnly) {
        return OperationCode::Failed;
    }

    if (m_modified
        || p_force
        || m_state & (StateFlag::FileMissingOnDisk | StateFlag::FileChangedOutside)) {
        syncContent();

        // We do not involve user here to handle file missing and changed outside cases.
        // The active ViewWindow will check this periodically.
        // Check if file still exists.
        if (!p_force && !checkFileExistsOnDisk()) {
            qWarning() << "failed to save buffer due to file missing on disk" << getPath();
            return OperationCode::FileMissingOnDisk;
        }

        // Check if file is modified outside.
        if (!p_force && checkFileChangedOutside()) {
            qWarning() << "failed to save buffer due to file changed from outside" << getPath();
            return OperationCode::FileChangedOutside;
        }

        try {
            m_provider->write(m_content);
        } catch (Exception &p_e) {
            qWarning() << "failed to write the buffer content" << getPath() << p_e.what();
            return OperationCode::Failed;
        }

        setModified(false);
        m_state &= ~(StateFlag::FileMissingOnDisk | StateFlag::FileChangedOutside);
    }
    return OperationCode::Success;
}

Buffer::OperationCode Buffer::reload()
{
    // Check if file is missing.
    if (!checkFileExistsOnDisk()) {
        qWarning() << "failed to save buffer due to file missing on disk" << getPath();
        return OperationCode::FileMissingOnDisk;
    }

    if (m_modified
        || m_state & (StateFlag::FileMissingOnDisk | StateFlag::FileChangedOutside)) {
        readContent();

        emit modified(m_modified);
        emit contentsChanged();
    }
    return OperationCode::Success;
}

void Buffer::readContent()
{
    m_content = m_provider->read();
    ++m_revision;

    // Reset state.
    m_viewWindowToSync = nullptr;
    m_modified = false;
}

void Buffer::discard()
{
    Q_ASSERT(!(m_state & StateFlag::Discarded));
    Q_ASSERT(m_attachedViewWindowCount == 1);
    m_autoSaveTimer->stop();
    m_content.clear();
    m_state |= StateFlag::Discarded;
    ++m_revision;

    m_viewWindowToSync = nullptr;
    m_modified = false;
}

void Buffer::close()
{
    // Delete the backup file if exists.
    m_autoSaveTimer->stop();
    if (!m_backupFilePath.isEmpty()) {
        FileUtils::removeFile(m_backupFilePath);
        m_backupFilePath.clear();
    }
}

QString Buffer::getImageFolderPath() const
{
    return const_cast<Buffer *>(this)->m_provider->fetchImageFolderPath();
}

QString Buffer::insertImage(const QString &p_srcImagePath, const QString &p_imageFileName)
{
    Q_UNUSED(p_srcImagePath);
    Q_UNUSED(p_imageFileName);
    Q_ASSERT_X(false, "insertImage", "image insert is not supported");
    return QString();
}

QString Buffer::insertImage(const QImage &p_image, const QString &p_imageFileName)
{
    Q_UNUSED(p_image);
    Q_UNUSED(p_imageFileName);
    Q_ASSERT_X(false, "insertImage", "image insert is not supported");
    return QString();
}

void Buffer::removeImage(const QString &p_imagePath)
{
    Q_UNUSED(p_imagePath);
    Q_ASSERT_X(false, "removeImage", "image remove is not supported");
}

void Buffer::autoSave()
{
    if (m_readOnly) {
        m_autoSaveTimer->stop();
        return;
    }

    if (m_state & (StateFlag::FileMissingOnDisk | StateFlag::FileChangedOutside)) {
        qDebug() << "disable AutoSave due to file missing on disk or changed outside";
        return;
    }
    Q_ASSERT(!(m_state & StateFlag::Discarded));
    auto policy = ConfigMgr::getInst().getEditorConfig().getAutoSavePolicy();
    switch (policy) {
    case EditorConfig::AutoSavePolicy::None:
        return;

    case EditorConfig::AutoSavePolicy::AutoSave:
        if (save(false) != OperationCode::Success) {
            qWarning() << "AutoSave failed to save buffer, retry later";
        }
        break;

    case EditorConfig::AutoSavePolicy::BackupFile:
        try {
            writeBackupFile();
        } catch (Exception &p_e) {
            qWarning() << "AutoSave failed to write backup file, retry later" << p_e.what();
        }
        break;
    }
}

void Buffer::writeBackupFile()
{
    if (m_backupFilePath.isEmpty()) {
        const auto &config = ConfigMgr::getInst().getEditorConfig();
        QString backupDirPath(QDir(getResourcePath()).filePath(config.getBackupFileDirectory()));
        backupDirPath = QDir::cleanPath(backupDirPath);
        auto backupFileName = FileUtils::generateFileNameWithSequence(backupDirPath,
                                                                      getName(),
                                                                      config.getBackupFileExtension());
        QDir backupDir(backupDirPath);
        backupDir.mkpath(backupDirPath);
        m_backupFilePath = backupDir.filePath(backupFileName);
    }

    Q_ASSERT(m_backupFilePathOfPreviousSession.isEmpty());

    // Just use FileUtils instead of notebook backend.
    FileUtils::writeFile(m_backupFilePath, generateBackupFileHead() + getContent());
}

QString Buffer::generateBackupFileHead() const
{
    return QString("vnotex_backup_file %1|").arg(getContentPath());
}

void Buffer::checkBackupFileOfPreviousSession()
{
    const auto &config = ConfigMgr::getInst().getEditorConfig();
    if (config.getAutoSavePolicy() != EditorConfig::AutoSavePolicy::BackupFile) {
        return;
    }

    QString backupDirPath(QDir(getResourcePath()).filePath(config.getBackupFileDirectory()));
    backupDirPath = QDir::cleanPath(backupDirPath);
    QDir backupDir(backupDirPath);
    QStringList backupFiles;
    {
        const QString nameFilter = QString("%1*%2").arg(getName(), config.getBackupFileExtension());
        backupFiles = backupDir.entryList(QStringList(nameFilter),
                                          QDir::Files | QDir::Hidden | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    }

    if (backupFiles.isEmpty()) {
        return;
    }

    for (const auto &file : backupFiles) {
        const auto filePath = backupDir.filePath(file);
        if (isBackupFileOfBuffer(filePath)) {
            const auto backupContent = readBackupFile(filePath);
            if (backupContent == getContent()) {
                // Found backup file with identical content.
                // Just discard the backup file.
                FileUtils::removeFile(filePath);
                qInfo() << "delete identical backup file of previous session" << filePath;
            } else {
                m_backupFilePathOfPreviousSession = filePath;
                qInfo() << "found backup file of previous session" << filePath;
            }
            break;
        }
    }
}

bool Buffer::isBackupFileOfBuffer(const QString &p_file) const
{
    QFile file(p_file);
    if (!file.open(QFile::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream st(&file);
    const auto head = st.readLine();
    return head.startsWith(generateBackupFileHead());
}

const QString &Buffer::getBackupFileOfPreviousSession() const
{
    return m_backupFilePathOfPreviousSession;
}

QString Buffer::readBackupFile(const QString &p_filePath)
{
    auto content = FileUtils::readTextFile(p_filePath);
    return content.mid(content.indexOf(QLatin1Char('|')) + 1);
}

void Buffer::discardBackupFileOfPreviousSession()
{
    Q_ASSERT(!m_backupFilePathOfPreviousSession.isEmpty());

    FileUtils::removeFile(m_backupFilePathOfPreviousSession);
    qInfo() << "discard backup file of previous session" << m_backupFilePathOfPreviousSession;
    m_backupFilePathOfPreviousSession.clear();
}

void Buffer::recoverFromBackupFileOfPreviousSession()
{
    Q_ASSERT(!m_backupFilePathOfPreviousSession.isEmpty());

    m_content = readBackupFile(m_backupFilePathOfPreviousSession);
    m_provider->write(m_content);
    ++m_revision;

    FileUtils::removeFile(m_backupFilePathOfPreviousSession);
    qInfo() << "recover from backup file of previous session" << m_backupFilePathOfPreviousSession;
    m_backupFilePathOfPreviousSession.clear();

    // Reset state.
    m_viewWindowToSync = nullptr;
    m_modified = false;

    emit modified(m_modified);
    emit contentsChanged();
}

bool Buffer::isChildOf(const Node *p_node) const
{
    return m_provider->isChildOf(p_node);
}

bool Buffer::isAttachmentSupported() const
{
    return !m_readOnly && m_provider->isAttachmentSupported();
}

bool Buffer::hasAttachment() const
{
    if (!isAttachmentSupported()) {
        return false;
    }

    if (m_provider->getAttachmentFolder().isEmpty()) {
        return false;
    }

    QDir dir(getAttachmentFolderPath());
    return !dir.isEmpty();
}

QString Buffer::getAttachmentFolderPath() const
{
    Q_ASSERT(isAttachmentSupported());
    return const_cast<Buffer *>(this)->m_provider->fetchAttachmentFolderPath();
}

QStringList Buffer::addAttachment(const QString &p_destFolderPath, const QStringList &p_files)
{
    if (p_files.isEmpty()) {
        return QStringList();
    }
    auto destFolderPath = p_destFolderPath.isEmpty() ? getAttachmentFolderPath() : p_destFolderPath;
    Q_ASSERT(PathUtils::pathContains(getAttachmentFolderPath(), destFolderPath));
    auto files = m_provider->addAttachment(destFolderPath, p_files);
    if (!files.isEmpty()) {
        emit attachmentChanged();
    }
    return files;
}

QString Buffer::newAttachmentFile(const QString &p_destFolderPath, const QString &p_name)
{
    Q_ASSERT(PathUtils::pathContains(getAttachmentFolderPath(), p_destFolderPath));
    auto filePath = m_provider->newAttachmentFile(p_destFolderPath, p_name);
    emit attachmentChanged();
    return filePath;
}

QString Buffer::newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name)
{
    Q_ASSERT(PathUtils::pathContains(getAttachmentFolderPath(), p_destFolderPath));
    auto folderPath = m_provider->newAttachmentFolder(p_destFolderPath, p_name);
    emit attachmentChanged();
    return folderPath;
}

QString Buffer::renameAttachment(const QString &p_path, const QString &p_name)
{
    Q_ASSERT(PathUtils::pathContains(getAttachmentFolderPath(), p_path));
    return m_provider->renameAttachment(p_path, p_name);
}

void Buffer::removeAttachment(const QStringList &p_paths)
{
    m_provider->removeAttachment(p_paths);
    emit attachmentChanged();
}

bool Buffer::isAttachment(const QString &p_path) const
{
    return PathUtils::pathContains(getAttachmentFolderPath(), p_path);
}

Buffer::ProviderType Buffer::getProviderType() const
{
    return m_provider->getType();
}

Node *Buffer::getNode() const
{
    return m_provider->getNode();
}

bool Buffer::checkFileExistsOnDisk()
{
    if (m_provider->checkFileExistsOnDisk()) {
        m_state &= ~StateFlag::FileMissingOnDisk;
        return true;
    } else {
        m_state |= StateFlag::FileMissingOnDisk;
        return false;
    }
}

bool Buffer::checkFileChangedOutside()
{
    if (m_provider->checkFileChangedOutside()) {
        m_state |= StateFlag::FileChangedOutside;
        return true;
    } else {
        m_state &= ~StateFlag::FileChangedOutside;
        return false;
    }
}

Buffer::StateFlags Buffer::state() const
{
    return m_state;
}

QSharedPointer<File> Buffer::getFile() const
{
    return m_provider->getFile();
}
