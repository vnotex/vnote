#ifndef MARKDOWNVIEWWINDOW_H
#define MARKDOWNVIEWWINDOW_H

#include "viewwindow.h"

#include <QScopedPointer>

class QSplitter;
class QStackedWidget;

namespace vte
{
    class MarkdownEditorConfig;
}

namespace vnotex
{
    struct FileOpenParameters;
    class MarkdownEditor;
    class MarkdownViewer;
    class EditorMarkdownViewerAdapter;
    class PreviewHelper;
    struct Outline;
    class MarkdownEditorConfig;

    class MarkdownViewWindow : public ViewWindow
    {
        Q_OBJECT
    public:
        friend class TextViewWindowHelper;

        MarkdownViewWindow(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent = nullptr);

        ~MarkdownViewWindow();

        QString getLatestContent() const Q_DECL_OVERRIDE;

        void setMode(Mode p_mode) Q_DECL_OVERRIDE;

        QSharedPointer<OutlineProvider> getOutlineProvider() Q_DECL_OVERRIDE;

    public slots:
        void handleEditorConfigChange() Q_DECL_OVERRIDE;

    protected slots:
        void setModified(bool p_modified) Q_DECL_OVERRIDE;

        void handleBufferChangedInternal() Q_DECL_OVERRIDE;

        void handleTypeAction(TypeAction p_action) Q_DECL_OVERRIDE;

        void handleSectionNumberOverride(OverrideState p_state) Q_DECL_OVERRIDE;

        void handleFindTextChanged(const QString &p_text, FindOptions p_options) Q_DECL_OVERRIDE;

        void handleFindNext(const QString &p_text, FindOptions p_options) Q_DECL_OVERRIDE;

        void handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText) Q_DECL_OVERRIDE;

        void handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText) Q_DECL_OVERRIDE;

        void handleFindAndReplaceWidgetClosed() Q_DECL_OVERRIDE;

        void handleFindAndReplaceWidgetOpened() Q_DECL_OVERRIDE;

    protected:
        void syncEditorFromBuffer() Q_DECL_OVERRIDE;

        void syncEditorFromBufferContent() Q_DECL_OVERRIDE;

        bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

        void detachFromBufferInternal() Q_DECL_OVERRIDE;

        void scrollUp() Q_DECL_OVERRIDE;

        void scrollDown() Q_DECL_OVERRIDE;

        void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupToolBar();

        void setupTextEditor();

        QStackedWidget *getMainStatusWidget() const;

        // Focus appropriate editor according to current mode.
        void focusEditor();

        void setupViewer();

        void syncTextEditorFromBuffer(bool p_syncPositionFromReadMode);

        void syncViewerFromBuffer(bool p_syncPositionFromEditMode);

        // Non-virtual function of syncEditorFromBufferContent().
        void doSyncEditorFromBufferContent(bool p_syncPosition);

        void syncTextEditorFromBufferContent(bool p_syncPosition);

        void syncViewerFromBufferContent(bool p_syncPosition);

        // When we have new changes to the buffer content from our ViewWindow,
        // we will invalidate the contents of the buffer and the buffer will
        // call this function to tell us now the latest buffer revision.
        void setBufferRevisionAfterInvalidation(int p_bufferRevision);

        EditorMarkdownViewerAdapter *adapter() const;

        // We need a non-virtual version for constructor.
        void setModeInternal(Mode p_mode);

        // Get the position to sync from editor.
        // Return -1 for an invalid position.
        int getEditLineNumber() const;

        // Get the position to sync from viewer.
        // Return -1 for an invalid position.
        int getReadLineNumber() const;

        void clearObsoleteImages();

        void setupOutlineProvider();

        void updateWebViewerConfig(const MarkdownEditorConfig &p_config);

        template <class T>
        static QSharedPointer<Outline> headingsToOutline(const QVector<T> &p_headings);

        static QSharedPointer<vte::MarkdownEditorConfig> createMarkdownEditorConfig(const MarkdownEditorConfig &p_config);

        bool m_initialized = false;

        // Splitter to hold editor and viewer.
        QSplitter *m_splitter = nullptr;

        // Managed by QObject.
        MarkdownEditor *m_editor = nullptr;

        // Managed by QObject.
        MarkdownViewer *m_viewer = nullptr;

        QSharedPointer<QWidget> m_textEditorStatusWidget;

        QSharedPointer<QWidget> m_viewerStatusWidget;

        QSharedPointer<QStackedWidget> m_mainStatusWidget;

        // Managed by QObject.
        PreviewHelper *m_previewHelper = nullptr;

        // Whether propogate the state from editor to buffer.
        bool m_propogateEditorToBuffer = false;

        int m_textEditorBufferRevision = 0;

        int m_viewerBufferRevision = 0;

        int m_markdownEditorConfigRevision = 0;

        Mode m_previousMode = Mode::Invalid;

        QSharedPointer<FileOpenParameters> m_openParas;

        QSharedPointer<OutlineProvider> m_outlineProvider;
    };
}

#endif // MARKDOWNVIEWWINDOW_H
