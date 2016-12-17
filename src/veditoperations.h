#ifndef VEDITOPERATIONS_H
#define VEDITOPERATIONS_H

#include <QPointer>
#include <QString>
#include "vfile.h"

class VEdit;
class QMimeData;
class QKeyEvent;

enum class KeyState { Normal = 0, Vim };

class VEditOperations
{
public:
    VEditOperations(VEdit *p_editor, VFile *p_file);
    virtual ~VEditOperations();
    virtual bool insertImageFromMimeData(const QMimeData *source) = 0;
    virtual bool insertURLFromMimeData(const QMimeData *source) = 0;
    virtual bool insertImage() = 0;
    // Return true if @p_event has been handled and no need to be further
    // processed.
    virtual bool handleKeyPressEvent(QKeyEvent *p_event) = 0;
    void updateTabSettings();

protected:
    void insertTextAtCurPos(const QString &p_text);

    VEdit *m_editor;
    QPointer<VFile> m_file;
    bool m_expandTab;
    QString m_tabSpaces;
    KeyState m_keyState;
};

#endif // VEDITOPERATIONS_H
