#ifndef MARKDOWNVIEWWINDOW_H
#define MARKDOWNVIEWWINDOW_H

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
    class MarkdownEditor;
    class MarkdownViewer;
    class MarkdownViewerAdapter;
    class PreviewHelper;
    struct Outline;
    class EditorConfig;
    class ImageHost;
    class SearchToken;

    class MarkdownViewWindow : public ViewWindow
    {
        Q_OBJECT
    public:
        friend class TextViewWindowHelper;

        MarkdownViewWindow(QWidget *p_parent = nullptr);

        ~MarkdownViewWindow();

        QString getLatestContent() const Q_DECL_OVERRIDE;

        QString selectedText() const Q_DECL_OVERRIDE;

        void setMode(ViewWindowMode p_mode) Q_DECL_OVERRIDE;

        QSharedPointer<OutlineProvider> getOutlineProvider() Q_DECL_OVERRIDE;

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

        void handleTypeAction(TypeAction p_action) Q_DECL_OVERRIDE;

        void handleSectionNumberOverride(OverrideState p_state) Q_DECL_OVERRIDE;

        void handleImageHostChanged(const QString &p_hostName) Q_DECL_OVERRIDE;

        void handleFindTextChanged(const QString &p_text, FindOptions p_options) Q_DECL_OVERRIDE;

        void handleFindNext(const QStringList &p_texts, FindOptions p_options) Q_DECL_OVERRIDE;

        void handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText) Q_DECL_OVERRIDE;

        void handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText) Q_DECL_OVERRIDE;

        void handleFindAndReplaceWidgetClosed() Q_DECL_OVERRIDE;

        void handleFindAndReplaceWidgetOpened() Q_DECL_OVERRIDE;

        void toggleDebug() Q_DECL_OVERRIDE;

        void print() Q_DECL_OVERRIDE;

        void clearHighlights() Q_DECL_OVERRIDE;

    protected:
        void syncEditorFromBuffer() Q_DECL_OVERRIDE;

        void syncEditorFromBufferContent() Q_DECL_OVERRIDE;

        bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

        void detachFromBufferInternal() Q_DECL_OVERRIDE;

        void scrollUp() Q_DECL_OVERRIDE;

        void scrollDown() Q_DECL_OVERRIDE;

        void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

        QPoint getFloatingWidgetPosition() Q_DECL_OVERRIDE;

        void updateViewModeMenu(QMenu *p_menu) Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupToolBar();

        void setupTextEditor();

        QStackedWidget *getMainStatusWidget() const;

        // Focus appropriate editor according to current mode.
        void focusEditor();

        void setupViewer();

        void setupPreviewHelper();

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

        MarkdownViewerAdapter *adapter() const;

        // Get the position to sync from editor.
        // Return -1 for an invalid position.
        int getEditLineNumber() const;

        // Get the position to sync from viewer.
        // Return -1 for an invalid position.
        int getReadLineNumber() const;

        void clearObsoleteImages();

        void setupOutlineProvider();

        void updateEditorFromConfig();

        void updateWebViewerConfig();

        void setModeInternal(ViewWindowMode p_mode, bool p_syncBuffer);

        void handleFileOpenParameters(const QSharedPointer<FileOpenParameters> &p_paras, bool p_twice);

        void scrollToLine(int p_lineNumber);

        void findTextBySearchToken(const QSharedPointer<SearchToken> &p_token, int p_currentMatchLine);

        bool isReadMode() const;

        void updatePreviewHelperFromConfig(const MarkdownEditorConfig &p_config);

        void removeFromImageHost(const QString &p_url);

        bool updateConfigRevision();

        void setupDebugViewer();

        void setEditViewMode(MarkdownEditorConfig::EditViewMode p_mode);

        void syncEditorContentsToPreview();

        void syncEditorPositionToPreview();

        void handleExternalCodeBlockHighlightRequest(int p_idx, quint64 p_timeStamp, const QString &p_text);

        template <class T>
        static QSharedPointer<Outline> headingsToOutline(const QVector<T> &p_headings);

        static QSharedPointer<vte::MarkdownEditorConfig> createMarkdownEditorConfig(const EditorConfig &p_editorConfig, const MarkdownEditorConfig &p_config);

        static QSharedPointer<vte::TextEditorParameters> createMarkdownEditorParameters(const EditorConfig &p_editorConfig, const MarkdownEditorConfig &p_config);

        // Splitter to hold editor and viewer.
        QSplitter *m_splitter = nullptr;

        // Managed by QObject.
        MarkdownEditor *m_editor = nullptr;

        // Managed by QObject.
        MarkdownViewer *m_viewer = nullptr;

        QSharedPointer<QWidget> m_textEditorStatusWidget;

        QSharedPointer<QWidget> m_viewerStatusWidget;

        QSharedPointer<QStackedWidget> m_mainStatusWidget;

        // Used to debug web view.
        QWebEngineView *m_debugViewer = nullptr;

        // Managed by QObject.
        PreviewHelper *m_previewHelper = nullptr;

        // Whether propogate the state from editor to buffer.
        bool m_propogateEditorToBuffer = false;

        int m_textEditorBufferRevision = 0;

        int m_viewerBufferRevision = 0;

        int m_textEditorConfigRevision = 0;

        int m_markdownEditorConfigRevision = 0;

        ViewWindowMode m_previousMode = ViewWindowMode::Invalid;

        QSharedPointer<OutlineProvider> m_outlineProvider;

        ImageHost *m_imageHost = nullptr;

        bool m_viewerReady = false;

        QActionGroup *m_viewModeActionGroup = nullptr;

        MarkdownEditorConfig::EditViewMode m_editViewMode = MarkdownEditorConfig::EditViewMode::EditOnly;

        QTimer *m_syncPreviewTimer = nullptr;
    };
}

#endif // MARKDOWNVIEWWINDOW_H
