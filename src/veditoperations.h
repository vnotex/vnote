#ifndef VEDITOPERATIONS_H
#define VEDITOPERATIONS_H

#include <QPointer>
#include "vfile.h"

class VEdit;
class QMimeData;

class VEditOperations
{
public:
    VEditOperations(VEdit *p_editor, VFile *p_file);
    virtual ~VEditOperations();
    virtual bool insertImageFromMimeData(const QMimeData *source) = 0;
    virtual bool insertURLFromMimeData(const QMimeData *source) = 0;
    virtual bool insertImage() = 0;

protected:
    void insertTextAtCurPos(const QString &text);
    VEdit *m_editor;
    QPointer<VFile> m_file;
};

#endif // VEDITOPERATIONS_H
