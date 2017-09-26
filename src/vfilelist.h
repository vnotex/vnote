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
    bool importFile(const QString &p_srcFilePath);
    inline void setEditArea(VEditArea *editArea);
    void fileInfo(VNoteFile *p_file);
    void deleteFile(VNoteFile *p_file);
    bool locateFile(const VNoteFile *p_file);
    inline const VDirectory *currentDirectory() const;

    // Implementations for VNavigationMode.
    void registerNavigation(QChar p_majorKey) Q_DECL_OVERRIDE;
    void showNavigation() Q_DECL_OVERRIDE;
    void hideNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

signals:
    void fileClicked(VNoteFile *p_file, OpenFileMode mode = OpenFileMode::Read);
    void fileCreated(VNoteFile *p_file, OpenFileMode mode = OpenFileMode::Read);
    void fileUpdated(const VNoteFile *p_file);

private slots:
    void contextMenuRequested(QPoint pos);
    void handleItemClicked(QListWidgetItem *currentItem);
    void fileInfo();
    void openFileLocation() const;
    // m_copiedFiles will keep the files's VNoteFile.
    void copySelectedFiles(bool p_isCut = false);
    void cutSelectedFiles();
    void pasteFilesInCurDir();
    void deleteFile();
    void handleRowsMoved(const QModelIndex &p_parent, int p_start, int p_end, const QModelIndex &p_destination, int p_row);

public slots:
    void setDirectory(VDirectory *p_directory);
    void newFile();

protected:
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    void focusInEvent(QFocusEvent *p_event) Q_DECL_OVERRIDE;

private:
    void setupUI();

    // Init shortcuts.
    void initShortcuts();

    void updateFileList();

    QListWidgetItem *insertFileListItem(VNoteFile *file, bool atFront = false);

    void removeFileListItem(QListWidgetItem *item);

    // Init actions.
    void initActions();

    // Return the corresponding QListWidgetItem of @p_file.
    QListWidgetItem *findItem(const VNoteFile *p_file);

    void copyFileInfoToClipboard(const QJsonArray &p_files, bool p_isCut);
    void pasteFiles(VDirectory *p_destDir);
    bool copyFile(VDirectory *p_destDir, const QString &p_destName, VNoteFile *p_file, bool p_cut);
    // New items have been added to direcotry. Update file list accordingly.
    QVector<QListWidgetItem *> updateFileListAdded();

    inline QPointer<VNoteFile> getVFile(QListWidgetItem *p_item) const;

    // Check if the list items match exactly the contents of the directory.
    bool identicalListWithDirectory() const;
    QList<QListWidgetItem *> getVisibleItems() const;

    // Fill the info of @p_item according to @p_file.
    void fillItem(QListWidgetItem *p_item, const VNoteFile *p_file);

    VEditArea *editArea;
    QListWidget *fileList;
    QPointer<VDirectory> m_directory;
    QVector<QPointer<VNoteFile> > m_copiedFiles;

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
