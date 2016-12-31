#ifndef VMDEDITOPERATIONS_H
#define VMDEDITOPERATIONS_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QImage>
#include "veditoperations.h"

class QTimer;

// Editor operations for Markdown
class VMdEditOperations : public VEditOperations
{
    Q_OBJECT
public:
    VMdEditOperations(VEdit *p_editor, VFile *p_file);
    bool insertImageFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;
    bool insertURLFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;
    bool insertImage() Q_DECL_OVERRIDE;
    bool handleKeyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void pendingTimerTimeout();

private:
    bool insertImageFromURL(const QUrl &imageUrl);
    void insertImageFromPath(const QString &title, const QString &path, const QString &oriImagePath);
    void insertImageFromQImage(const QString &title, const QString &path, const QImage &image);
    void setKeyState(KeyState p_state);

    // Key press handlers.
    bool handleKeyTab(QKeyEvent *p_event);
    bool handleKeyBackTab(QKeyEvent *p_event);
    bool handleKeyB(QKeyEvent *p_event);
    bool handleKeyD(QKeyEvent *p_event);
    bool handleKeyH(QKeyEvent *p_event);
    bool handleKeyI(QKeyEvent *p_event);
    bool handleKeyU(QKeyEvent *p_event);
    bool handleKeyW(QKeyEvent *p_event);
    bool handleKeyEsc(QKeyEvent *p_event);
    bool handleKeyPressVim(QKeyEvent *p_event);
    bool handleKeyBracketLeft(QKeyEvent *p_event);
    bool shouldTriggerVimMode(QKeyEvent *p_event);
    int keySeqToNumber(const QList<QString> &p_seq);
    bool suffixNumAllowed(const QList<QString> &p_seq);

    QTimer *m_pendingTimer;
};

#endif // VMDEDITOPERATIONS_H
