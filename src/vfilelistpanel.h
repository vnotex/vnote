#ifndef VFILELISTPANEL_H
#define VFILELISTPANEL_H

#include <QWidget>
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include "vnotebook.h"

class VNote;
class VFileList;
class QPushButton;
class QListWidgetItem;

class VFileListPanel : public QWidget
{
    Q_OBJECT
public:
    VFileListPanel(VNote *vnote, QWidget *parent = 0);
    bool importFile(const QString &name);

signals:
    void fileClicked(QJsonObject fileJson);
    void fileDeleted(QJsonObject fileJson);
    void fileCreated(QJsonObject fileJson);
    void fileRenamed(const QString &notebook, const QString &oldPath,
                     const QString &newPath);

public slots:
    void setDirectory(QJsonObject dirJson);
    void handleNotebookRenamed(const QVector<VNotebook> &notebooks, const QString &oldName,
                               const QString &newName);
    void handleDirectoryRenamed(const QString &notebook, const QString &oldRelativePath,
                                const QString &newRelativePath);

private slots:
    void onNewFileBtnClicked();
    void onDeleteFileBtnClicked();
    void onFileInfoBtnClicked();

private:
    void setupUI();
    bool isConflictNameWithExisting(const QString &name);
    void renameFile(QListWidgetItem *item, const QString &newName);
    void clearDirectoryInfo();
    inline QString getDirectoryName();

    VNote *vnote;
    QString notebook;
    // Current directory's relative path
    QString relativePath;
    // Used for cache
    QString rootPath;

    VFileList *fileList;
    QPushButton *newFileBtn;
    QPushButton *deleteFileBtn;
    QPushButton *fileInfoBtn;
};

inline QString VFileListPanel::getDirectoryName()
{
    if (relativePath.isEmpty()) {
        return "";
    }
    return QFileInfo(QDir::cleanPath(relativePath)).fileName();
}
#endif // VFILELISTPANEL_H
