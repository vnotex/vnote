#ifndef VEDITOPERATIONS_H
#define VEDITOPERATIONS_H

#include <QPointer>
#include <QString>
#include <QObject>
#include <QList>
#include "vfile.h"
#include "utils/vvim.h"

class VEdit;
class VEditConfig;
class QMimeData;
class QKeyEvent;

class VEditOperations: public QObject
{
    Q_OBJECT
public:
    VEditOperations(VEdit *p_editor, VFile *p_file);

    virtual ~VEditOperations();

    virtual bool insertImageFromMimeData(const QMimeData *source) = 0;

    virtual bool insertImage() = 0;

    virtual bool insertImageFromURL(const QUrl &p_imageUrl) = 0;

    virtual bool insertLink(const QString &p_linkText,
                            const QString &p_linkUrl) = 0;

    // Return true if @p_event has been handled and no need to be further
    // processed.
    virtual bool handleKeyPressEvent(QKeyEvent *p_event) = 0;

    // Request to propogate Vim status.
    void requestUpdateVimStatus();

    // Insert decoration markers or decorate selected text.
    virtual void decorateText(TextDecoration p_decoration);

    // Set Vim mode if not NULL.
    void setVimMode(VimMode p_mode);

signals:
    // Want to display a template message in status bar.
    void statusMessage(const QString &p_msg);

    // Propogate Vim status.
    void vimStatusUpdated(const VVim *p_vim);

protected slots:
    // Handle the update of VEditConfig of the editor.
    virtual void handleEditConfigUpdated();

    // Vim mode changed.
    void handleVimModeChanged(VimMode p_mode);

private:
    // Update m_editConfig->m_cursorLineBg.
    void updateCursorLineBg();

protected:
    void insertTextAtCurPos(const QString &p_text);

    VEdit *m_editor;
    QPointer<VFile> m_file;
    VEditConfig *m_editConfig;
    VVim *m_vim;
};

#endif // VEDITOPERATIONS_H
