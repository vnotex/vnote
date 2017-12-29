#ifndef VWEBVIEW_H
#define VWEBVIEW_H

#include <QWebEngineView>
#include <QClipboard>

class VFile;
class QMenu;

class VWebView : public QWebEngineView
{
    Q_OBJECT
public:
    // @p_file could be NULL.
    explicit VWebView(VFile *p_file, QWidget *p_parent = Q_NULLPTR);

signals:
    void editNote();

protected:
    void contextMenuEvent(QContextMenuEvent *p_event);

private slots:
    void handleEditAction();

    void handleCopyImageUrlAction();

    void handleCopyWithoutBackgroundAction();

    // Copy the clicked image.
    // Used to replace the default CopyImageToClipboard action.
    void copyImage();

    void handleClipboardChanged(QClipboard::Mode p_mode);

private:
    void hideUnusedActions(QMenu *p_menu);

    void alterHtmlMimeData(QClipboard *p_clipboard,
                           const QMimeData *p_mimeData,
                           bool p_removeBackground);

    bool fixImgSrc(QString &p_html);

    VFile *m_file;

    // Whether this view has hooked the Copy Image Url action.
    bool m_copyImageUrlActionHooked;

    bool m_needRemoveBackground;

    bool m_fixImgSrc;
};

#endif // VWEBVIEW_H
