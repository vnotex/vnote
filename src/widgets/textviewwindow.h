#ifndef TEXTVIEWWINDOW_H
#define TEXTVIEWWINDOW_H

#include "viewwindow.h"

namespace vte
{
    class TextEditorConfig;
}

namespace vnotex
{
    class TextEditor;
    class TextEditorConfig;

    class TextViewWindow : public ViewWindow
    {
        Q_OBJECT
    public:
        friend class TextViewWindowHelper;

        explicit TextViewWindow(QWidget *p_parent = nullptr);

        QString getLatestContent() const Q_DECL_OVERRIDE;

        void setMode(Mode p_mode) Q_DECL_OVERRIDE;

    public slots:
        void handleEditorConfigChange() Q_DECL_OVERRIDE;

    protected slots:
        void setModified(bool p_modified) Q_DECL_OVERRIDE;

        void handleBufferChangedInternal() Q_DECL_OVERRIDE;

    protected:
        void syncEditorFromBuffer() Q_DECL_OVERRIDE;

        void syncEditorFromBufferContent() Q_DECL_OVERRIDE;

        void scrollUp() Q_DECL_OVERRIDE;

        void scrollDown() Q_DECL_OVERRIDE;

        void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupToolBar();

        // When we have new changes to the buffer content from our ViewWindow,
        // we will invalidate the contents of the buffer and the buffer will
        // call this function to tell us now the latest buffer revision.
        void setBufferRevisionAfterInvalidation(int p_bufferRevision);

        static QSharedPointer<vte::TextEditorConfig> createTextEditorConfig(const TextEditorConfig &p_config);

        // Managed by QObject.
        TextEditor *m_editor = nullptr;

        // Whether propogate the state from editor to buffer.
        bool m_propogateEditorToBuffer = false;

        int m_textEditorConfigRevision = 0;
    };
}

#endif // TEXTVIEWWINDOW_H
