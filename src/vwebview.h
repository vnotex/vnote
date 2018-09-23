#ifndef VWEBVIEW_H
#define VWEBVIEW_H

#include <QWebEngineView>
#include <QClipboard>
#include <QRegExp>

class VFile;
class QMenu;

class VWebView : public QWebEngineView
{
    Q_OBJECT
public:
    // @p_file could be NULL.
    explicit VWebView(VFile *p_file, QWidget *p_parent = Q_NULLPTR);

    void setInPreview(bool p_preview);

signals:
    void editNote();

    void requestSavePage();

    void requestExpandRestorePreviewArea();

protected:
    void contextMenuEvent(QContextMenuEvent *p_event);

private slots:
    void handleEditAction();

    void handleCopyImageUrlAction();

    void handleCopyAsAction(QAction *p_act);

    void handleCopyAllAsAction(QAction *p_act);

    // Copy the clicked image.
    // Used to replace the default CopyImageToClipboard action.
    void copyImage();

    void handleClipboardChanged(QClipboard::Mode p_mode);

private:
    void hideUnusedActions(QMenu *p_menu);

    void alterHtmlMimeData(QClipboard *p_clipboard,
                           const QMimeData *p_mimeData,
                           const QString &p_copyTarget);

    void removeHtmlFromImageData(QClipboard *p_clipboard,
                                 const QMimeData *p_mimeData);

    bool removeStyles(QString &p_html);

    void initCopyAsMenu(QAction *p_after, QMenu *p_menu);

    void initCopyAllAsMenu(QMenu *p_menu);

    void initPreviewTunnelMenu(QAction *p_before, QMenu *p_menu);

    VFile *m_file;

    // Whether this view has hooked the Copy Image Url action.
    bool m_copyImageUrlActionHooked;

    // Whether it is after copy image action.
    bool m_afterCopyImage;

    // Target of Copy As.
    QString m_copyTarget;

    // Whether in preview mode.
    bool m_inPreview;
};

inline void VWebView::setInPreview(bool p_preview)
{
    m_inPreview = p_preview;
}
#endif // VWEBVIEW_H
