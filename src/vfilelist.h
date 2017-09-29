#ifndef VFILELIST_H
#define VFILELIST_H

#include <QWidget>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <QListWidgetItem>
#include <QMap>
#include "vnotebook.h"
#include "vconstants.h"
#include "vdirectory.h"
#include "vnotefile.h"
#include "vnavigationmode.h"

class QAction;
class VNote;
class QListWidget;
class QPushButton;
class VEditArea;
class QFocusEvent;
class QLabel;

class VFileList : public QWidget, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VFileList(QWidget *parent = 0);

    // Import external files @p_files to current directory.
    // Only copy the files itself.
    bool importFiles(const QStringList &p_files, QString *p_errMsg = NULL);

    inline void setEditArea(VEditArea *editArea);

    // View and edit information of @p_file.
    void fileInfo(VNoteFile *p_file);

    // Delete file @p_file.
    // It is not necessary that @p_file exists in the list.
    void deleteFile(VNoteFile *p_file);

    // Locate @p_file in the list widget.
    bool locateFile(const VNoteFile *p_file);

    inline const VDirectory *currentDirectory() const;

    // Implementations for VNavigationMode.
    void registerNavigation(QChar p_majorKey) Q_DECL_OVERRIDE;
    void showNavigation() Q_DECL_OVERRIDE;
    void hideNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

public slots:
    // Set VFileList to display content of @p_directory directory.
    void setDirectory(VDirectory *p_directory);

    // Create a note.
    void newFile();

signals:
    void fileClicked(VNoteFile *p_file, OpenFileMode mode = OpenFileMode::Read);
    void fileCreated(VNoteFile *p_file, OpenFileMode mode = OpenFileMode::Read);
    void fileUpdated(const VNoteFile *p_file);

private slots:
    void contextMenuRequested(QPoint pos);
    void handleItemClicked(QListWidgetItem *currentItem);

    // View and edit information of selected file.
    // Valid only when there is only one selected file.
    void fileInfo();

    // Open the folder containing selected file in system's file browser.
    // Valid only when there is only one selected file.
    void openFileLocation() const;

    // Copy selected files to clipboard.
    // Will put a Json string into the clipboard which contains the information
    // about copied files.
    void copySelectedFiles(bool p_isCut = false);

    void cutSelectedFiles();

    // Paste files from clipboard.
    void pasteFilesFromClipboard();

    // Delete selected files.
    void deleteSelectedFiles();

    // Delete files @p_files.
    void deleteFiles(const QVector<VNoteFile *> &p_files);

    // Sort files in this list.
    void sortItems();

protected:
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    void focusInEvent(QFocusEvent *p_event) Q_DECL_OVERRIDE;

private:
    void setupUI();

    // Init shortcuts.
    void initShortcuts();

    // Clear and re-fill the list widget according to m_directory.
    void updateFileList();

    // Insert a new item into the list widget.
    // @file: the file represented by the new item.
    // @atFront: insert at the front or back of the list widget.
    QListWidgetItem *insertFileListItem(VNoteFile *file, bool atFront = false);

    // Remove and delete item related to @p_file from list widget.
    void removeFileListItem(VNoteFile *p_file);

    // Init actions.
    void initActions();

    // Return the corresponding QListWidgetItem of @p_file.
    QListWidgetItem *findItem(const VNoteFile *p_file);

    // Paste files given path by @p_files to destination directory @p_destDir.
    void pasteFiles(VDirectory *p_destDir,
                    const QVector<QString> &p_files,
                    bool p_isCut);

    // New items have been added to direcotry. Update file list accordingly.
    QVector<QListWidgetItem *> updateFileListAdded();

    inline QPointer<VNoteFile> getVFile(QListWidgetItem *p_item) const;

    QList<QListWidgetItem *> getVisibleItems() const;

    // Fill the info of @p_item according to @p_file.
    void fillItem(QListWidgetItem *p_item, const VNoteFile *p_file);

    // Generate new magic to m_magicForClipboard.
    int getNewMagic();

    // Check if @p_magic equals to m_magicForClipboard.
    bool checkMagic(int p_magic) const;

    // Check if there are files in clipboard available to paste.
    bool pasteAvailable() const;

    VEditArea *editArea;
    QListWidget *fileList;
    QPointer<VDirectory> m_directory;

    // Magic number for clipboard operations.
    int m_magicForClipboard;

    // Actions
    QAction *m_openInReadAct;
    QAction *m_openInEditAct;
    QAction *m_openExternalAct;
    QAction *newFileAct;
    QAction *deleteFileAct;
    QAction *fileInfoAct;
    QAction *copyAct;
    QAction *cutAct;
    QAction *pasteAct;
    QAction *m_openLocationAct;
    QAction *m_sortAct;

    // Navigation Mode.
    // Map second key to QListWidgetItem.
    QMap<QChar, QListWidgetItem *> m_keyMap;
    QVector<QLabel *> m_naviLabels;

    static const QString c_infoShortcutSequence;
    static const QString c_copyShortcutSequence;
    static const QString c_cutShortcutSequence;
    static const QString c_pasteShortcutSequence;
};

inline void VFileList::setEditArea(VEditArea *editArea)
{
    this->editArea = editArea;
}

inline QPointer<VNoteFile> VFileList::getVFile(QListWidgetItem *p_item) const
{
    Q_ASSERT(p_item);
    return (VNoteFile *)p_item->data(Qt::UserRole).toULongLong();
}

inline const VDirectory *VFileList::currentDirectory() const
{
    return m_directory;
}

#endif // VFILELIST_H
