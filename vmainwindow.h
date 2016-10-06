#ifndef VMAINWINDOW_H
#define VMAINWINDOW_H

#include <QMainWindow>

class QLabel;
class QComboBox;
class VDirectoryTree;
class QSplitter;
class QListWidget;
class QTabWidget;
class VNote;
class VFileList;
class VTabWidget;

class VMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    VMainWindow(QWidget *parent = 0);
    ~VMainWindow();

private slots:
    // Change current notebook index and update the directory tree
    void setCurNotebookIndex(int index);

signals:
    void curNotebookIndexChanged(const QString &path);

private:
    void setupUI();
    // Update notebookComboBox according to vnote
    void updateNotebookComboBox();

    QLabel *notebookLabel;
    QComboBox *notebookComboBox;
    VDirectoryTree *directoryTree;
    VFileList *fileList;
    VTabWidget *tabs;
    QSplitter *mainSplitter;
    VNote *vnote;
};

#endif // VMAINWINDOW_H
