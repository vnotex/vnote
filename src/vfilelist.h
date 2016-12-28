#ifndef VFILELIST_H
#define VFILELIST_H

#include <QWidget>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <QListWidgetItem>
#include "vnotebook.h"
#include "vconstants.h"
#include "vdirectory.h"
#include "vfile.h"

class QAction;
class VNote;
class QListWidget;
class QPushButton;
class VEditArea;

class VFileList : public QWidget
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

signals:
    void fileClicked(VFile *p_file, OpenFileMode mode = OpenFileMode::Read);
    void fileCreated(VFile *p_file, OpenFileMode mode = OpenFileMode::Read);
    void fileUpdated(const VFile *p_file);

private slots:
    void contextMenuRequested(QPoint pos);
    void handleItemClicked(QListWidgetItem *currentItem);
    void fileInfo();
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

private:
    void setupUI();
    void updateFileList();
    QListWidgetItem *insertFileListItem(VFile *file, bool atFront = false);
    void removeFileListItem(QListWidgetItem *item);
    void initActions();
    QListWidgetItem *findItem(const VFile *p_file);
    void copyFileInfoToClipboard(const QJsonArray &p_files, bool p_isCut);
    void pasteFiles(VDirectory *p_destDir);
    bool copyFile(VDirectory *p_destDir, const QString &p_destName, VFile *p_file, bool p_cut);
    // New items have been added to direcotry. Update file list accordingly.
    QVector<QListWidgetItem *> updateFileListAdded();
    inline QPointer<VFile> getVFile(QListWidgetItem *p_item) const;
    // Check if the list items match exactly the contents of the directory.
    bool identicalListWithDirectory() const;

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
