#ifndef MARKDOWNVIEWER_H
#define MARKDOWNVIEWER_H

#include "../webviewer.h"

#include <QClipboard>

namespace vnotex
{
    class MarkdownViewerAdapter;
    class PreviewHelper;

    class MarkdownViewer : public WebViewer
    {
        Q_OBJECT
    public:
        // @p_adapter will be managed by MarkdownViewer.
        MarkdownViewer(MarkdownViewerAdapter *p_adapter,
                       const QColor &p_background,
                       qreal p_zoomFactor,
                       QWidget *p_parent = nullptr);

        MarkdownViewerAdapter *adapter() const;

        void setPreviewHelper(PreviewHelper *p_previewHelper);

    signals:
        void zoomFactorChanged(qreal p_factor);

        void editRequested();

    protected:
        void contextMenuEvent(QContextMenuEvent *p_event) Q_DECL_OVERRIDE;

    private slots:
        void handleClipboardChanged(QClipboard::Mode p_mode);

        void handleWebKeyPress(int p_key, bool p_ctrl, bool p_shift, bool p_meta);

    private:
        void handleCopyImageUrlAction();

        // Copy the clicked image.
        // Used to replace the default CopyImageToClipboard action.
        void copyImage();

        void removeHtmlFromImageData(QClipboard *p_clipboard, const QMimeData *p_mimeData);

        void hideUnusedActions(QMenu *p_menu);

        void zoomOut();

        void zoomIn();

        void restoreZoom();

        void setupCrossCopyMenu(QMenu *p_menu, QAction *p_copyAct);

        // @p_baseUrl: if it is a folder, please end it with '/'. It is not used now in web side.
        void crossCopy(const QString &p_target, const QString &p_baseUrl, const QString &p_html);

        // Managed by QObject.
        MarkdownViewerAdapter *m_adapter = nullptr;

        // Whether this view has hooked the Copy Image Url action.
        bool m_copyImageUrlActionHooked = false;

        // Whether CopyImage action has been triggered.
        bool m_copyImageTriggered = false;

        // Target name of cross copy going to execute.
        QString m_crossCopyTarget;
    };
}

#endif // MARKDOWNVIEWER_H
