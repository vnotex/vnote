#ifndef ATTACHMENTPOPUP_H
#define ATTACHMENTPOPUP_H

#include <QMenu>

class QToolButton;

namespace vnotex
{
    class FileSystemViewer;
    class Buffer;

    class AttachmentPopup : public QMenu
    {
        Q_OBJECT
    public:
        AttachmentPopup(QToolButton *p_btn, QWidget *p_parent = nullptr);

        void setBuffer(Buffer *p_buffer);

    private:
        void setupUI();

        QToolButton *createButton();

        bool checkRootFolderAndSingleSelection();

        void addAttachments(const QString &p_destFolderPath, const QStringList &p_files);

        void setRootFolder(const QString &p_folderPath);

        QString getDestFolderPath() const;

        void newAttachmentFile(const QString &p_destFolderPath, const QString &p_name);

        void newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name);

        void showPopupLater(const QStringList &p_pathsToSelect = QStringList());

        Buffer *m_buffer = nullptr;

        // Managed by QObject.
        FileSystemViewer *m_viewer = nullptr;

        bool m_needUpdateAttachmentFolder = true;

        // Button for this menu.
        QToolButton *m_button = nullptr;
    };
}

#endif // ATTACHMENTPOPUP_H
