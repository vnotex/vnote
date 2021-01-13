#ifndef BUFFER_H
#define BUFFER_H

#include <QObject>
#include <QSharedPointer>

#include <functional>

#include <global.h>

class QWidget;
class QTimer;

namespace vnotex
{
    class Node;
    class Buffer;
    class ViewWindow;
    struct FileOpenParameters;
    class BufferProvider;
    class File;

    struct BufferParameters
    {
        QSharedPointer<BufferProvider> m_provider;
    };

    class Buffer : public QObject
    {
        Q_OBJECT
    public:
        enum class ProviderType
        {
            Internal,
            External
        };

        enum class OperationCode
        {
            Success,
            FileMissingOnDisk,
            FileChangedOutside,
            Failed
        };

        enum StateFlag
        {
            Normal = 0,
            FileMissingOnDisk = 0x1,
            FileChangedOutside = 0x2,
            Discarded = 0x4
        };
        Q_DECLARE_FLAGS(StateFlags, StateFlag);

        Buffer(const BufferParameters &p_parameters,
               QObject *p_parent = nullptr);

        virtual ~Buffer();

        int getAttachViewWindowCount() const;

        void attachViewWindow(ViewWindow *p_win);
        void detachViewWindow(ViewWindow *p_win);

        // Create a view window to show the content of this buffer.
        // Attach the created view window to this buffer.
        ViewWindow *createViewWindow(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent);

        // Whether this buffer matches @p_node.
        bool match(const Node *p_node) const;

        // Whether this buffer matches @p_filePath.
        bool match(const QString &p_filePath) const;

        // Buffer name.
        QString getName() const;

        QString getPath() const;

        // In some cases, getPath() may point to a ocntainer containting all the stuffs.
        // getContentPath() will return the real path to the file providing the content.
        QString getContentPath() const;

        // Get the base path to resolve resources.
        QString getResourcePath() const;

        // Return nullptr if not available.
        QSharedPointer<File> getFile() const;

        ID getID() const;

        // Get buffer content.
        // It may differ from the content on disk.
        // For performance, we need to sync the content with ViewWindow before returning
        // the latest content.
        const QString &getContent() const;

        // @p_revision will be set before contentsChanged is emitted.
        void setContent(const QString &p_content, int &p_revision);

        // Invalidate the content of buffer.
        // Need to sync with @p_win to get the latest content.
        // @p_setRevision will be called to set revision before contentsChanged is emitted.
        void invalidateContent(const ViewWindow *p_win,
                               const std::function<void(int)> &p_setRevision);

        // Sync content with @p_win if @p_win is the window needed to sync.
        void syncContent(const ViewWindow *p_win);

        int getRevision() const;

        bool isModified() const;
        void setModified(bool p_modified);

        bool isReadOnly() const;

        // Save buffer content to file.
        OperationCode save(bool p_force);

        // Discard changes and reload file.
        OperationCode reload();

        // Discard the buffer which will invalidate the buffer.
        void discard();

        // Buffer is about to be deleted.
        void close();

        // Insert image from @p_srcImagePath.
        // Return inserted image file path.
        virtual QString insertImage(const QString &p_srcImagePath, const QString &p_imageFileName);

        virtual QString insertImage(const QImage &p_image, const QString &p_imageFileName);

        virtual void removeImage(const QString &p_imagePath);

        const QString &getBackupFileOfPreviousSession() const;

        void discardBackupFileOfPreviousSession();

        void recoverFromBackupFileOfPreviousSession();

        // Whether this buffer's provider is a child of @p_node or an attachment of @p_node.
        bool isChildOf(const Node *p_node) const;

        Node *getNode() const;

        bool isAttachmentSupported() const;

        bool hasAttachment() const;

        QString getAttachmentFolderPath() const;

        // @p_destFolderPath: folder path locating in attachment folder. Use the root folder if empty.
        QStringList addAttachment(const QString &p_destFolderPath, const QStringList &p_files);

        QString newAttachmentFile(const QString &p_destFolderPath, const QString &p_name);

        QString newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name);

        QString renameAttachment(const QString &p_path, const QString &p_name);

        void removeAttachment(const QStringList &p_paths);

        // Judge whether file @p_path is attachment.
        bool isAttachment(const QString &p_path) const;

        ProviderType getProviderType() const;

        bool checkFileExistsOnDisk();

        bool checkFileChangedOutside();

        StateFlags state() const;

        static QString readBackupFile(const QString &p_filePath);

    signals:
        void attachedViewWindowEmpty();

        void modified(bool p_modified);

        void contentsChanged();

        void nameChanged();

        void attachmentChanged();

    protected:
        virtual ViewWindow *createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent) = 0;

        QSharedPointer<BufferProvider> m_provider;

    private slots:
        void autoSave();

    private:
        void syncContent();

        void readContent();

        // Get the path of the image folder.
        QString getImageFolderPath() const;

        void writeBackupFile();

        // Generate backup file head.
        QString generateBackupFileHead() const;

        void checkBackupFileOfPreviousSession();

        bool isBackupFileOfBuffer(const QString &p_file) const;

        // Will be assigned uniquely once created.
        const ID c_id = 0;

        // Revision of contents.
        int m_revision = 0;

        // If the buffer is modified, m_content reflect the latest changes instead
        // of the file content.
        QString m_content;

        bool m_readOnly = false;

        bool m_modified = false;

        int m_attachedViewWindowCount = 0;

        const ViewWindow *m_viewWindowToSync = nullptr;

        // Managed by QObject.
        QTimer *m_autoSaveTimer = nullptr;

        QString m_backupFilePath;

        QString m_backupFilePathOfPreviousSession;

        StateFlags m_state = StateFlag::Normal;
    };
} // ns vnotex

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::Buffer::StateFlags)

#endif // BUFFER_H
