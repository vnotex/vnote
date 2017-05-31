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
#include "vfile.h"
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
    void fileInfo(VFile *p_file);
    void deleteFile(VFile *p_file);
    bool locateFile(const VFile *p_file);
    inline const VDirectory *currentDirectory() const;

    // Implementations for VNavigationMode.
    void registerNavigation(QChar p_majorKey) Q_DECL_OVERRIDE;
    void showNavigation() Q_DECL_OVERRIDE;
    void hideNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

signals:
    void fileClicked(VFile *p_file, OpenFileMode mode = OpenFileMode::Read);
    void fileCreated(VFile *p_file, OpenFileMode mode = OpenFileMode::Read);
    void fileUpdated(const VFile *p_file);

private slots:
    void contextMenuRequested(QPoint pos);
    void handleItemClicked(QListWidgetItem *currentItem);
    void fileInfo();
    void openFileLocation() const;
    // m_copiedFiles will keep the files's VFile.
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
    void updateFileList();
    QListWidgetItem *insertFileListItem(VFile *file, bool atFront = false);
    void removeFileListItem(QListWidgetItem *item);
    void initActions();

    // Return the corresponding QListWidgetItem of @p_file.
    QListWidgetItem *findItem(const VFile *p_file);

    void copyFileInfoToClipboard(const QJsonArray &p_files, bool p_isCut);
    void pasteFiles(VDirectory *p_destDir);
    bool copyFile(VDirectory *p_destDir, const QString &p_destName, VFile *p_file, bool p_cut);
    // New items have been added to direcotry. Update file list accordingly.
    QVector<QListWidgetItem *> updateFileListAdded();
    inline QPointer<VFile> getVFile(QListWidgetItem *p_item) const;
    // Check if the list items match exactly the contents of the directory.
    bool identicalListWithDirectory() const;
    QList<QListWidgetItem *> getVisibleItems() const;

    // @p_file: the file to be renamed or copied.
    // @p_newFilePath: the new file path of @p_file.
    // Check if the rename/copy will change the DocType. If yes, then ask
    // user for confirmation.
    // Return true if we can continue.
    bool promptForDocTypeChange(const VFile *p_file, const QString &p_newFilePath);

    // Fill the info of @p_item according to @p_file.
    void fillItem(QListWidgetItem *p_item, const VFile *p_file);

    VEditArea *editArea;
    QListWidget *fileList;
    QPointer<VDirectory> m_directory;
    QVector<QPointer<VFile> > m_copiedFiles;

    // Actions
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
};

inline void VFileList::setEditArea(VEditArea *editArea)
{
    this->editArea = editArea;
}

inline QPointer<VFile> VFileList::getVFile(QListWidgetItem *p_item) const
{
    Q_ASSERT(p_item);
    return (VFile *)p_item->data(Qt::UserRole).toULongLong();
}

inline const VDirectory *VFileList::currentDirectory() const
{
    return m_directory;
}

#endif // VFILELIST_H
