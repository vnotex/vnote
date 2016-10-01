#ifndef VMAINWINDOW_H
#define VMAINWINDOW_H

#include <QMainWindow>

class QLabel;
class QComboBox;
class VDirectoryTree;
class QSplitter;
class QListWidget;
class QTabWidget;

class VMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    VMainWindow(QWidget *parent = 0);
    ~VMainWindow();

private:
    void setupUI();

    QLabel *notebookLabel;
    QComboBox *notebookComboBox;
    VDirectoryTree *directoryTree;
    QListWidget *fileListWidget;
    QTabWidget *editorTabWidget;
    QSplitter *mainSplitter;
};

#endif // VMAINWINDOW_H
