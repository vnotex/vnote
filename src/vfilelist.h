#ifndef VFILELIST_H
#define VFILELIST_H

#include <QWidget>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include "vnotebook.h"

class QAction;
class VNote;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class VFileList : public QWidget
{
    Q_OBJECT
public:
    explicit VFileList(VNote *vnote, QWidget *parent = 0);
    bool importFile(const QString &name);

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
    void onNewFileBtnClicked();
    void onDeleteFileBtnClicked();
    void onFileInfoBtnClicked();

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
    inline QString getDirectoryName();
    void renameFile(QListWidgetItem *item, const QString &newName);

    VNote *vnote;
    QString notebook;
    // Current directory's relative path
    QString relativePath;
    // Used for cache
    QString rootPath;

    QListWidget *fileList;
    QPushButton *newFileBtn;
    QPushButton *deleteFileBtn;
    QPushButton *fileInfoBtn;

    // Actions
    QAction *newFileAct;
    QAction *deleteFileAct;
};

inline QString VFileList::getDirectoryName()
{
    if (relativePath.isEmpty()) {
        return "";
    }
    return QFileInfo(QDir::cleanPath(relativePath)).fileName();
}

#endif // VFILELIST_H
