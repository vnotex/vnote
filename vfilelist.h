#ifndef VFILELIST_H
#define VFILELIST_H

#include <QListWidget>
#include <QJsonObject>

class QAction;

class VFileList : public QListWidget
{
    Q_OBJECT
public:
    explicit VFileList(QWidget *parent = 0);

signals:
    void fileClicked(QJsonObject fileJson);

private slots:
    void newFile();
    void deleteFile();
    void contextMenuRequested(QPoint pos);
    void handleItemClicked(QListWidgetItem *currentItem);

public slots:
    void setDirectory(QJsonObject dirJson);

private:
    void updateFileList();
    QListWidgetItem *insertFileListItem(QJsonObject fileJson, bool atFront = false);
    void removeFileListItem(QListWidgetItem *item);
    void initActions();
    bool isConflictNameWithExisting(const QString &name);
    QListWidgetItem *createFileAndUpdateList(const QString &name,
                                             const QString &description);
    void deleteFileAndUpdateList(QListWidgetItem *item);
    void clearDirectoryInfo();

    QString rootPath;
    QString relativePath;
    QString directoryName;

    // Actions
    QAction *newFileAct;
    QAction *deleteFileAct;
};

#endif // VFILELIST_H
