#ifndef VFILELIST_H
#define VFILELIST_H

#include <QWidget>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include "vnotebook.h"
#include "vconstants.h"

class QAction;
class VNote;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class VEditArea;

class VFileList : public QWidget
{
    Q_OBJECT
public:
    explicit VFileList(VNote *vnote, QWidget *parent = 0);
    bool importFile(const QString &name);
    inline void setEditArea(VEditArea *editArea);
    void fileInfo(const QString &p_notebook, const QString &p_relativePath);

signals:
    void fileClicked(QJsonObject fileJson);
    void fileDeleted(QJsonObject fileJson);
    void fileCreated(QJsonObject fileJson);
    void fileRenamed(const QString &notebook, const QString &oldPath,
                     const QString &newPath);
    void directoryChanged(const QString &notebook, const QString &relativePath);

private slots:
    void contextMenuRequested(QPoint pos);
    void handleItemClicked(QListWidgetItem *currentItem);
    void curFileInfo();

public slots:
    void setDirectory(QJsonObject dirJson);
    void handleNotebookRenamed(const QVector<VNotebook> &notebooks, const QString &oldName,
                               const QString &newName);
    void handleDirectoryRenamed(const QString &notebook, const QString &oldRelativePath,
                                const QString &newRelativePath);
    void newFile();
    void deleteFile();

private:
    void setupUI();
    void updateFileList();
    QListWidgetItem *insertFileListItem(QJsonObject fileJson, bool atFront = false);
    void removeFileListItem(QListWidgetItem *item);
    void initActions();
    bool isConflictNameWithExisting(const QString &name);
    QListWidgetItem *createFileAndUpdateList(const QString &name);
    void deleteFileAndUpdateList(QListWidgetItem *item);
    void clearDirectoryInfo();
    void renameFile(const QString &p_notebook,
                    const QString &p_relativePath, const QString &p_newName);
    void convertFileType(const QString &notebook, const QString &fileRelativePath,
                         DocType oldType, DocType newType);
    QListWidgetItem *findItem(const QString &p_notebook, const QString &p_relativePath);

    VNote *vnote;
    QString notebook;
    // Current directory's relative path
    QString relativePath;
    // Used for cache
    QString rootPath;

    VEditArea *editArea;

    QListWidget *fileList;

    // Actions
    QAction *newFileAct;
    QAction *deleteFileAct;
    QAction *fileInfoAct;
};

inline void VFileList::setEditArea(VEditArea *editArea)
{
    this->editArea = editArea;
}

#endif // VFILELIST_H
