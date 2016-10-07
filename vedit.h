#ifndef VEDIT_H
#define VEDIT_H

#include <QTextEdit>
#include <QString>
#include "vconstants.h"
#include "vnotefile.h"

class VEdit : public QTextEdit
{
    Q_OBJECT
public:
    VEdit(VNoteFile *noteFile, QWidget *parent = 0);
    void beginEdit();
    bool tryEndEdit();

    // begin: sync the buffer to noteFile->content;
    // end: setModified(false)
    void beginSave();
    void endSave();

    void reloadFile();

signals:

public slots:


private:
    VNoteFile *noteFile;
};

#endif // VEDIT_H
