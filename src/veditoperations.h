#ifndef VEDITOPERATIONS_H
#define VEDITOPERATIONS_H

class VNoteFile;
class VEdit;
class QMimeData;

class VEditOperations
{
public:
    VEditOperations(VEdit *editor, VNoteFile *noteFile);
    virtual ~VEditOperations();
    virtual bool insertImageFromMimeData(const QMimeData *source) = 0;
    virtual bool insertURLFromMimeData(const QMimeData *source) = 0;

protected:
    void insertTextAtCurPos(const QString &text);
    VEdit *editor;
    VNoteFile *noteFile;
};

#endif // VEDITOPERATIONS_H
