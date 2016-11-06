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

class VNote;

class VEditArea : public QWidget
{
    Q_OBJECT
public:
    explicit VEditArea(VNote *vnote, QWidget *parent = 0);

signals:
    void curTabStatusChanged(const QString &notebook, const QString &relativePath,
                             bool editMode, bool modifiable);

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

public slots:
    void openFile(QJsonObject fileJson);
    // Close the file forcely
    void closeFile(QJsonObject fileJson);
    void editFile();
    void saveFile();
    void readFile();
    void saveAndReadFile();
    void handleNotebookRenamed(const QVector<VNotebook> &notebooks, const QString &oldName,
                               const QString &newName);

private slots:
    void handleSplitWindowRequest(VEditWindow *curWindow);
    void handleRemoveSplitRequest(VEditWindow *curWindow);
    void handleWindowFocused();

private:
    void setupUI();
    QVector<QPair<int, int> > findTabsByFile(const QString &notebook, const QString &relativePath);
    int openFileInWindow(int windowIndex, const QString &notebook, const QString &relativePath,
                         int mode);
    void setCurrentTab(int windowIndex, int tabIndex, bool setFocus);
    void setCurrentWindow(int windowIndex, bool setFocus);
    inline VEditWindow *getWindow(int windowIndex) const;
    void insertSplitWindow(int idx);
    void removeSplitWindow(VEditWindow *win);
    void noticeTabStatus();

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
