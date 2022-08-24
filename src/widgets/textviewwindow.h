#ifndef TEXTVIEWWINDOW_H
#define TEXTVIEWWINDOW_H

#include "viewwindow.h"

namespace vte
{
    class TextEditorConfig;
    struct TextEditorParameters;
}

namespace vnotex
{
    class TextEditor;
    class TextEditorConfig;
    class EditorConfig;

    class TextViewWindow : public ViewWindow
    {
        Q_OBJECT
    public:
        friend class TextViewWindowHelper;

        explicit TextViewWindow(QWidget *p_parent = nullptr);

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

        void handleFindTextChanged(const QString &p_text, FindOptions p_options) Q_DECL_OVERRIDE;

        void handleFindNext(const QStringList &p_texts, FindOptions p_options) Q_DECL_OVERRIDE;

        void handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText) Q_DECL_OVERRIDE;

        void handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText) Q_DECL_OVERRIDE;

        void handleFindAndReplaceWidgetClosed() Q_DECL_OVERRIDE;

        void print() Q_DECL_OVERRIDE;

    protected:
        void syncEditorFromBuffer() Q_DECL_OVERRIDE;

        void syncEditorFromBufferContent() Q_DECL_OVERRIDE;

        void scrollUp() Q_DECL_OVERRIDE;

        void scrollDown() Q_DECL_OVERRIDE;

        void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

        QPoint getFloatingWidgetPosition() Q_DECL_OVERRIDE;

        void clearHighlights() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupToolBar();

        // When we have new changes to the buffer content from our ViewWindow,
        // we will invalidate the contents of the buffer and the buffer will
        // call this function to tell us now the latest buffer revision.
        void setBufferRevisionAfterInvalidation(int p_bufferRevision);

        void updateEditorFromConfig();

        void handleFileOpenParameters(const QSharedPointer<FileOpenParameters> &p_paras);

        bool updateConfigRevision();

        static QSharedPointer<vte::TextEditorConfig> createTextEditorConfig(const EditorConfig &p_editorConfig, const TextEditorConfig &p_config);

        static QSharedPointer<vte::TextEditorParameters> createTextEditorParameters(const EditorConfig &p_editorConfig, const TextEditorConfig &p_config);

        // Managed by QObject.
        TextEditor *m_editor = nullptr;

        // Whether propogate the state from editor to buffer.
        bool m_propogateEditorToBuffer = false;

        int m_textEditorConfigRevision = 0;
    };
}

#endif // TEXTVIEWWINDOW_H
