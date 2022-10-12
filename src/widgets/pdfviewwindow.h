#ifndef PDFVIEWWINDOW_H
#define PDFVIEWWINDOW_H

#include "viewwindow.h"

#include <QScopedPointer>

#include <core/markdowneditorconfig.h>

class QSplitter;
class QStackedWidget;
class QWebEngineView;
class QActionGroup;
class QTimer;

namespace vte
{
    class MarkdownEditorConfig;
    struct TextEditorParameters;
}

namespace vnotex
{
    class PdfViewer;
    class PdfViewerAdapter;
    class EditorConfig;

    class PdfViewWindow : public ViewWindow
    {
        Q_OBJECT
    public:
        PdfViewWindow(QWidget *p_parent = nullptr);

        // ~PdfViewWindow();

        QString getLatestContent() const Q_DECL_OVERRIDE;

        QString selectedText() const Q_DECL_OVERRIDE;

        void setMode(ViewWindowMode p_mode) Q_DECL_OVERRIDE;

        void openTwice(const QSharedPointer<FileOpenParameters> &p_paras) Q_DECL_OVERRIDE;

        ViewWindowSession saveSession() const Q_DECL_OVERRIDE;

        void applySnippet(const QString &p_name) Q_DECL_OVERRIDE;

        void applySnippet() Q_DECL_OVERRIDE;

        void fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const Q_DECL_OVERRIDE;

    public slots:
        void handleEditorConfigChange() Q_DECL_OVERRIDE;

    protected slots:
        void setModified(bool p_modified) Q_DECL_OVERRIDE;

        void handleBufferChangedInternal(const QSharedPointer<FileOpenParameters> &p_paras) Q_DECL_OVERRIDE;

        void print() Q_DECL_OVERRIDE;

    protected:
        void syncEditorFromBuffer() Q_DECL_OVERRIDE;

        void syncEditorFromBufferContent() Q_DECL_OVERRIDE;

        void scrollUp() Q_DECL_OVERRIDE;

        void scrollDown() Q_DECL_OVERRIDE;

        void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupViewer();

        void setModeInternal(bool p_syncBuffer);

        void syncViewerFromBuffer();

        void syncViewerFromBufferContent();

        PdfViewerAdapter *adapter() const;

        // Managed by QObject.
        PdfViewer *m_viewer = nullptr;
    };
}

#endif // PDFVIEWWINDOW_H
