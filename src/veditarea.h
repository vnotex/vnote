#ifndef VEDITAREA_H
#define VEDITAREA_H

#include <QWidget>
#include <QJsonObject>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include <QVector>
#include <QPair>
#include <QtDebug>
#include <QSplitter>
#include "vnotebook.h"
#include "veditwindow.h"
#include "vtoc.h"

class VNote;
class VFile;
class VDirectory;

class VEditArea : public QWidget
{
    Q_OBJECT
public:
    explicit VEditArea(VNote *vnote, QWidget *parent = 0);
    bool isFileOpened(const VFile *p_file);
    bool closeAllFiles(bool p_forced);

signals:
    void curTabStatusChanged(const VFile *p_file, bool p_editMode);
    void outlineChanged(const VToc &toc);
    void curHeaderChanged(const VAnchor &anchor);

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

public slots:
    void openFile(VFile *p_file, OpenFileMode p_mode);
    bool closeFile(const VFile *p_file, bool p_forced);
    void editFile();
    void saveFile();
    void readFile();
    void saveAndReadFile();
    void handleOutlineItemActivated(const VAnchor &anchor);
    void handleFileUpdated(const VFile *p_file);
    void handleDirectoryUpdated(const VDirectory *p_dir);

private slots:
    void handleSplitWindowRequest(VEditWindow *curWindow);
    void handleRemoveSplitRequest(VEditWindow *curWindow);
    void handleWindowFocused();
    void handleOutlineChanged(const VToc &toc);
    void handleCurHeaderChanged(const VAnchor &anchor);

private:
    void setupUI();
    QVector<QPair<int, int> > findTabsByFile(const VFile *p_file);
    int openFileInWindow(int windowIndex, VFile *p_file, OpenFileMode p_mode);
    void setCurrentTab(int windowIndex, int tabIndex, bool setFocus);
    void setCurrentWindow(int windowIndex, bool setFocus);
    inline VEditWindow *getWindow(int windowIndex) const;
    void insertSplitWindow(int idx);
    void removeSplitWindow(VEditWindow *win);
    void updateWindowStatus();

    VNote *vnote;
    int curWindowIndex;

    // Splitter holding multiple split windows
    QSplitter *splitter;
};

inline VEditWindow* VEditArea::getWindow(int windowIndex) const
{
    return dynamic_cast<VEditWindow *>(splitter->widget(windowIndex));
}

#endif // VEDITAREA_H
