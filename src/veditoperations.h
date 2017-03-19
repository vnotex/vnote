#ifndef VEDITOPERATIONS_H
#define VEDITOPERATIONS_H

#include <QPointer>
#include <QString>
#include <QObject>
#include <QList>
#include "vfile.h"

class VEdit;
class QMimeData;
class QKeyEvent;

enum class KeyState { Normal = 0, Vim, VimVisual};

class VEditOperations: public QObject
{
    Q_OBJECT
public:
    VEditOperations(VEdit *p_editor, VFile *p_file);
    virtual ~VEditOperations();
    virtual bool insertImageFromMimeData(const QMimeData *source) = 0;
    virtual bool insertImage() = 0;
    virtual bool insertImageFromURL(const QUrl &p_imageUrl) = 0;
    // Return true if @p_event has been handled and no need to be further
    // processed.
    virtual bool handleKeyPressEvent(QKeyEvent *p_event) = 0;
    void updateTabSettings();

signals:
    void keyStateChanged(KeyState p_state);

protected:
    void insertTextAtCurPos(const QString &p_text);

    VEdit *m_editor;
    QPointer<VFile> m_file;
    bool m_expandTab;
    QString m_tabSpaces;
    KeyState m_keyState;
    // Seconds for pending mode.
    int m_pendingTime;
    QList<QString> m_pendingKey;
};

#endif // VEDITOPERATIONS_H
