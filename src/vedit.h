#ifndef VEDIT_H
#define VEDIT_H

#include <QTextEdit>
#include <QString>
#include <QPointer>
#include "vconstants.h"
#include "vtoc.h"
#include "vfile.h"

class VEditOperations;

class VEdit : public QTextEdit
{
    Q_OBJECT
public:
    VEdit(VFile *p_file, QWidget *p_parent = 0);
    virtual ~VEdit();
    virtual void beginEdit();
    virtual void endEdit();
    // Save buffer content to VFile.
    virtual void saveFile();
    virtual void setModified(bool p_modified);
    bool isModified() const;
    virtual void reloadFile();
    virtual void scrollToLine(int p_lineNumber);
    // User requests to insert an image.
    virtual void insertImage();

protected:
    QPointer<VFile> m_file;
    VEditOperations *m_editOps;
};



#endif // VEDIT_H
